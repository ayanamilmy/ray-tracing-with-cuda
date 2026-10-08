// Persistent animation renderer; the transport kernel is unchanged.
#include <cuda_runtime.h>
#include <cuda.h>
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#include <optix_stack_size.h>
#include "launch.h"
#include "geometry.h"
#include "frame_transport.h"
#include "launch_plan.h"
#include "mapped_transport.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cstdlib>
static_assert(OPTIX_VERSION==90000,"Use the pinned OptiX 9.0 headers for this backend.");

static void cuda_check(cudaError_t r,const char *op) {
    if(r!=cudaSuccess) throw std::runtime_error(std::string(op)+": "+cudaGetErrorString(r));
}
static void ox(OptixResult r,const char *op) {
    if(r!=OPTIX_SUCCESS) throw std::runtime_error(std::string(op)+": "+optixGetErrorName(r)+" / "+optixGetErrorString(r));
}
static void message(unsigned level,const char *tag,const char *text,void*) {
    std::cerr<<"OptiX["<<level<<"]["<<tag<<"] "<<text<<'\n';
}
struct Buffer {
    void *data=nullptr;
    explicit Buffer(size_t n) {if(n) cuda_check(cudaMalloc(&data,n),"allocate");}
    template<class T> explicit Buffer(const std::vector<T> &v):Buffer(v.size()*sizeof(T)) {
        if(data) upload(v.data(),v.size()*sizeof(T));
    }
    Buffer(const Buffer&)=delete;Buffer &operator=(const Buffer&)=delete;
    void upload(const void *p,size_t bytes) {cuda_check(cudaMemcpy(data,p,bytes,cudaMemcpyHostToDevice),"upload");}
    CUdeviceptr address() const {return reinterpret_cast<CUdeviceptr>(data);}
    template<class T> T *as() const {return static_cast<T*>(data);}
    ~Buffer() {if(data) cudaFree(data);}
};
struct Stream {
    cudaStream_t value=nullptr;
    Stream() {cuda_check(cudaStreamCreateWithFlags(&value,cudaStreamNonBlocking),"create stream");}
    ~Stream() {if(value) {cudaStreamSynchronize(value);cudaStreamDestroy(value);}}
};
struct Event {
    cudaEvent_t value=nullptr;
    Event() {cuda_check(cudaEventCreate(&value),"create timing event");}
    ~Event() {if(value) cudaEventDestroy(value);}
};
struct HostImage {
    pt::uchar4 *data=nullptr;
    explicit HostImage(size_t pixels) {cuda_check(cudaMallocHost(reinterpret_cast<void**>(&data),pixels*sizeof(*data)),"allocate readback");}
    ~HostImage() {if(data) cudaFreeHost(data);}
};
struct Pipeline {
    OptixDeviceContext context=nullptr;OptixModule module=nullptr;OptixPipeline pipeline=nullptr;
    std::vector<OptixProgramGroup> groups;
    ~Pipeline() {
        if(pipeline) optixPipelineDestroy(pipeline);
        for(auto g:groups) optixProgramGroupDestroy(g);
        if(module) optixModuleDestroy(module);
        if(context) optixDeviceContextDestroy(context);
    }
    OptixProgramGroup group(OptixProgramGroupDesc desc) {
        OptixProgramGroupOptions options{};OptixProgramGroup result=nullptr;char log[8192];size_t size=sizeof(log);
        auto r=optixProgramGroupCreate(context,&desc,1,&options,log,&size,&result);
        if(r!=OPTIX_SUCCESS) std::cerr.write(log,std::min(size,sizeof(log)));
        ox(r,"create program group");groups.push_back(result);return result;
    }
};
template<class Data> struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) Record {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];Data data;
};
struct Empty { unsigned unused=0; };
static std::unique_ptr<Buffer> acceleration(OptixDeviceContext context,OptixBuildInput input,
                                           OptixTraversableHandle &handle) {
    OptixAccelBuildOptions options{};options.buildFlags=OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    options.operation=OPTIX_BUILD_OPERATION_BUILD;OptixAccelBufferSizes sizes{};
    ox(optixAccelComputeMemoryUsage(context,&options,&input,1,&sizes),"size acceleration structure");
    Buffer scratch(sizes.tempSizeInBytes);auto result=std::make_unique<Buffer>(sizes.outputSizeInBytes);
    ox(optixAccelBuild(context,nullptr,&options,&input,1,scratch.address(),sizes.tempSizeInBytes,
                       result->address(),sizes.outputSizeInBytes,&handle,nullptr,0),"build acceleration structure");
    cuda_check(cudaDeviceSynchronize(),"acceleration completion");return result;
}
int main(int argc,char **argv) {try {
    if(argc<5 || argc>8) {
        std::cerr<<"Usage: optix_sequence device.ptx scene.npt -|--pipe|--mmap passes [tile_pixels]\n";return 2;
    }
    const bool pipe=std::string(argv[3])=="--pipe";
    const bool shared=std::string(argv[3])=="--mmap";
    const bool legacy_sync=std::getenv("NPT_LEGACY_SYNC") && std::string(std::getenv("NPT_LEGACY_SYNC"))=="1";
    std::ostream &log_out=pipe?std::cerr:std::cout;
    std::ios::sync_with_stdio(false);
    bool verify=false;
    bool software=false;
    if(argc>7 && !verify && !software) throw std::runtime_error("Last argument must be verify or software");
    auto s=pt::load_scene(argv[2]);auto geom=rt::geometry(s);
    unsigned passes=pt::positive_arg(argv[4]),tile=argc>5?pt::positive_arg(argv[5]):262144;
    std::ifstream file(argv[1],std::ios::binary);if(!file) throw std::runtime_error("Cannot open device PTX");
    std::string ptx((std::istreambuf_iterator<char>(file)),{});if(ptx.empty()) throw std::runtime_error("Empty PTX");
    cuda_check(cudaSetDevice(0),"select GPU");cuda_check(cudaFree(nullptr),"initialize CUDA context");
    cudaDeviceProp prop{};cuda_check(cudaGetDeviceProperties(&prop,0),"device properties");
    int driver=0;cuda_check(cudaDriverGetVersion(&driver),"driver version");
    log_out<<"GPU: "<<prop.name<<", CUDA driver "<<driver<<", OptiX "<<OPTIX_VERSION<<std::endl;
    ox(optixInit(),"OptiX initialization (requires libnvoptix.so.1 and NVIDIA R570+)");
    Pipeline engine;OptixDeviceContextOptions context_options{};
    context_options.logCallbackFunction=message;context_options.logCallbackLevel=3;
    context_options.validationMode=verify?OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL:OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_OFF;
    ox(optixDeviceContextCreate(nullptr,&context_options,&engine.context),"create OptiX context");
    auto setup_start=std::chrono::steady_clock::now();
    OptixModuleCompileOptions module_options{};module_options.optLevel=OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    module_options.debugLevel=OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;
    OptixPipelineCompileOptions compile{};compile.traversableGraphFlags=OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    compile.numPayloadValues=2;compile.numAttributeValues=2;compile.pipelineLaunchParamsVariableName="launch";
    compile.usesPrimitiveTypeFlags=OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE|OPTIX_PRIMITIVE_TYPE_FLAGS_CUSTOM;
    char log[16384];size_t log_size=sizeof(log);
    auto status=optixModuleCreate(engine.context,&module_options,&compile,ptx.data(),ptx.size(),log,&log_size,&engine.module);
    if(status!=OPTIX_SUCCESS) std::cerr.write(log,std::min(log_size,sizeof(log)));ox(status,"compile PTX module");
    OptixProgramGroupDesc desc{};desc.kind=OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    desc.raygen.module=engine.module;desc.raygen.entryFunctionName="__raygen__trace";auto trace=engine.group(desc);
    desc.raygen.entryFunctionName="__raygen__encode";auto encode=engine.group(desc);
    desc={};desc.kind=OPTIX_PROGRAM_GROUP_KIND_MISS;desc.miss.module=engine.module;
    desc.miss.entryFunctionName="__miss__empty";auto miss=engine.group(desc);
    desc={};desc.kind=OPTIX_PROGRAM_GROUP_KIND_HITGROUP;desc.hitgroup.moduleCH=desc.hitgroup.moduleAH=engine.module;
    desc.hitgroup.entryFunctionNameCH="__closesthit__surface";desc.hitgroup.entryFunctionNameAH="__anyhit__filter";
    auto triangle=engine.group(desc);
    desc.hitgroup.moduleIS=engine.module;desc.hitgroup.entryFunctionNameIS="__intersection__sphere";auto sphere=engine.group(desc);
    OptixPipelineLinkOptions link{};link.maxTraceDepth=1;log_size=sizeof(log);
    status=optixPipelineCreate(engine.context,&compile,&link,engine.groups.data(),unsigned(engine.groups.size()),log,&log_size,&engine.pipeline);
    if(status!=OPTIX_SUCCESS) std::cerr.write(log,std::min(log_size,sizeof(log)));ox(status,"link OptiX pipeline");
    OptixStackSizes stack{};for(auto group:engine.groups) ox(optixUtilAccumulateStackSizes(group,&stack,engine.pipeline),"accumulate stack");
    unsigned traversal,direct,continuation;
    ox(optixUtilComputeStackSizes(&stack,1,0,0,&traversal,&direct,&continuation),"compute stack sizes");
    ox(optixPipelineSetStackSize(engine.pipeline,traversal,direct,continuation,2),"set pipeline stack");

    Buffer vertices(geom.vertices),bounds(geom.bounds),tri_indices(geom.triangles),sphere_indices(geom.spheres);
    std::vector<std::unique_ptr<Buffer>> structures;std::vector<OptixInstance> instances;
    // Separate GASes: OptiX cannot mix triangle and custom types in one GAS.
    unsigned flags=OPTIX_GEOMETRY_FLAG_REQUIRE_SINGLE_ANYHIT_CALL;
    auto instance=[&](OptixTraversableHandle handle,unsigned sbt) {
        OptixInstance value{};value.transform[0]=value.transform[5]=value.transform[10]=1;
        value.instanceId=sbt;value.sbtOffset=sbt;value.visibilityMask=255;value.traversableHandle=handle;
        instances.push_back(value);
    };
    if(!geom.triangles.empty()) {
        OptixBuildInput input{};input.type=OPTIX_BUILD_INPUT_TYPE_TRIANGLES;auto &t=input.triangleArray;
        CUdeviceptr address=vertices.address();t.vertexBuffers=&address;t.numVertices=unsigned(geom.vertices.size());
        t.vertexFormat=OPTIX_VERTEX_FORMAT_FLOAT3;t.vertexStrideInBytes=sizeof(rt::Vertex);
        t.flags=&flags;t.numSbtRecords=1;OptixTraversableHandle handle;
        structures.push_back(acceleration(engine.context,input,handle));instance(handle,0);
    }
    if(!geom.spheres.empty()) {
        OptixBuildInput input{};input.type=OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;auto &a=input.customPrimitiveArray;
        CUdeviceptr address=bounds.address();a.aabbBuffers=&address;a.numPrimitives=unsigned(geom.spheres.size());
        a.strideInBytes=sizeof(rt::Bounds);a.flags=&flags;a.numSbtRecords=1;OptixTraversableHandle handle;
        structures.push_back(acceleration(engine.context,input,handle));instance(handle,1);
    }
    Buffer instance_data(instances);OptixBuildInput input{};input.type=OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    input.instanceArray.instances=instance_data.address();input.instanceArray.numInstances=unsigned(instances.size());
    OptixTraversableHandle handle;structures.push_back(acceleration(engine.context,input,handle));
    Record<Empty> rg_trace{},rg_encode{},ms{};
    ox(optixSbtRecordPackHeader(trace,&rg_trace),"pack raygen");ox(optixSbtRecordPackHeader(encode,&rg_encode),"pack encoder");
    ox(optixSbtRecordPackHeader(miss,&ms),"pack miss");
    std::vector<Record<rt::HitData>> hits(2);ox(optixSbtRecordPackHeader(triangle,&hits[0]),"pack triangle");
    ox(optixSbtRecordPackHeader(sphere,&hits[1]),"pack sphere");
    hits[0].data.indices=tri_indices.as<unsigned>();hits[1].data.indices=sphere_indices.as<unsigned>();
    Buffer rg(sizeof(rg_trace)),ms_buffer(sizeof(ms)),hit_buffer(hits);rg.upload(&rg_trace,sizeof(rg_trace));ms_buffer.upload(&ms,sizeof(ms));
    OptixShaderBindingTable sbt{};sbt.raygenRecord=rg.address();sbt.missRecordBase=ms_buffer.address();
    sbt.missRecordStrideInBytes=sizeof(ms);sbt.missRecordCount=1;
    sbt.hitgroupRecordBase=hit_buffer.address();sbt.hitgroupRecordStrideInBytes=sizeof(hits[0]);sbt.hitgroupRecordCount=2;
    size_t n=size_t(s.params.image.x)*s.params.image.y;tile=unsigned(std::min<size_t>(tile,n));
    Buffer primitives(s.primitives),materials(s.materials),pixels(s.pixels);
    // Allocate the reference BVH only for validation. Pure launches never read it.
    std::vector<pt::GPUNode> reference_nodes;if(verify || software) reference_nodes=s.nodes;Buffer nodes(reference_nodes);
    Buffer rng(n*sizeof(pt::GPURandom)),frame(n*sizeof(pt::float4)),accum(n*sizeof(pt::float4)),bgra(n*sizeof(pt::uchar4));
    Buffer counters(2*sizeof(unsigned)),parameters(sizeof(rt::Launch));
    size_t tiles=(n+tile-1)/tile;
    std::vector<rt::Launch> queued_params;
    queued_params.reserve(size_t(passes+1)*tiles);
    Buffer queued_parameters(size_t(passes+1)*tiles*sizeof(rt::Launch));
    Buffer encode_record(sizeof(rg_encode));encode_record.upload(&rg_encode,sizeof(rg_encode));
    auto encode_sbt=sbt;encode_sbt.raygenRecord=encode_record.address();
    HostImage image(n);
    Stream stream;
    Event trace_start,trace_end;
    cuda_check(cudaMemsetAsync(accum.data,0,n*sizeof(pt::float4),stream.value),"clear accumulation");
    cuda_check(cudaMemsetAsync(counters.data,0,2*sizeof(unsigned),stream.value),"clear validation counters");
    rt::Launch params{};params.handle=handle;params.primitives=primitives.as<pt::GPUPrimitive>();params.nodes=nodes.as<pt::GPUNode>();
    params.materials=materials.as<pt::GPUMaterial>();params.pixels=pixels.as<pt::uchar>();params.rng=rng.as<pt::GPURandom>();
    params.frame=frame.as<pt::float4>();params.accum=accum.as<pt::float4>();params.bgra=bgra.as<pt::uchar4>();
    params.verify=verify;params.software=software;params.errors=counters.as<unsigned>();
    double setup_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-setup_start).count();

    log_out<<"READY"<<std::endl;
    std::string command;
    std::unique_ptr<rt::MappedBuffer> mapped_frame,mapped_image;
    std::string mapped_frame_path,mapped_image_path;
    for(;;) {
      std::ifstream file_packet;
      std::unique_ptr<rt::MemoryInput> memory_input;
      std::unique_ptr<std::istream> memory_packet;
      std::string packet_path,output_path;
      if(!pipe) {
        if(!std::getline(std::cin,command) || command=="QUIT") break;
        auto split=command.find('\t');
        if(split==std::string::npos) throw std::runtime_error("Expected frame packet TAB output path");
        packet_path=command.substr(0,split);output_path=command.substr(split+1);
        if(shared) {
            if(!mapped_frame) {
                size_t length=16+sizeof(pt::GPUParams)+s.primitives.size()*sizeof(pt::GPUPrimitive)+s.materials.size()*sizeof(pt::GPUMaterial);
                mapped_frame=std::make_unique<rt::MappedBuffer>(packet_path,length,true);
                mapped_image=std::make_unique<rt::MappedBuffer>(output_path,20+n*sizeof(pt::uchar4)+1048576,false);
                mapped_frame_path=packet_path;mapped_image_path=output_path;
            }
            if(packet_path!=mapped_frame_path || output_path!=mapped_image_path) throw std::runtime_error("Mapped paths changed");
            memory_input=std::make_unique<rt::MemoryInput>(mapped_frame->data,mapped_frame->size);
            memory_packet=std::make_unique<std::istream>(memory_input.get());
        } else {
            file_packet.open(packet_path,std::ios::binary);
            if(!file_packet) throw std::runtime_error("Cannot open frame packet");
        }
      }
      auto frame_start=std::chrono::steady_clock::now();
      pt::GPUParams next{};
      std::istream &packet=pipe?std::cin:shared?*memory_packet:static_cast<std::istream&>(file_packet);
      if(!rt::read_frame(packet,next,s.primitives,s.materials,s.params)) {
        if(pipe) break;
        throw std::runtime_error("Empty frame packet");
      }
      if(!pipe && packet.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Extra frame packet bytes");
      auto next_geom=rt::geometry(s);
      if(next_geom.triangles!=geom.triangles || next_geom.spheres!=geom.spheres)
        throw std::runtime_error("Animation changed primitive topology");
      geom=std::move(next_geom);s.params=next;s.params.tone.y=s.params.tone.z=0;
      vertices.upload(geom.vertices.data(),geom.vertices.size()*sizeof(rt::Vertex));
      if(!geom.bounds.empty()) bounds.upload(geom.bounds.data(),geom.bounds.size()*sizeof(rt::Bounds));
      primitives.upload(s.primitives.data(),s.primitives.size()*sizeof(pt::GPUPrimitive));
      materials.upload(s.materials.data(),s.materials.size()*sizeof(pt::GPUMaterial));
      // Rebuild moving geometry, retain the context, pipeline, textures and allocations.
      structures.clear();instances.clear();
    if(!geom.triangles.empty()) {
        OptixBuildInput input{};input.type=OPTIX_BUILD_INPUT_TYPE_TRIANGLES;auto &t=input.triangleArray;
        CUdeviceptr address=vertices.address();t.vertexBuffers=&address;t.numVertices=unsigned(geom.vertices.size());
        t.vertexFormat=OPTIX_VERTEX_FORMAT_FLOAT3;t.vertexStrideInBytes=sizeof(rt::Vertex);
        t.flags=&flags;t.numSbtRecords=1;OptixTraversableHandle handle;
        structures.push_back(acceleration(engine.context,input,handle));instance(handle,0);
    }
    if(!geom.spheres.empty()) {
        OptixBuildInput input{};input.type=OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;auto &a=input.customPrimitiveArray;
        CUdeviceptr address=bounds.address();a.aabbBuffers=&address;a.numPrimitives=unsigned(geom.spheres.size());
        a.strideInBytes=sizeof(rt::Bounds);a.flags=&flags;a.numSbtRecords=1;OptixTraversableHandle handle;
        structures.push_back(acceleration(engine.context,input,handle));instance(handle,1);
    }
    instance_data.upload(instances.data(),instances.size()*sizeof(OptixInstance));OptixBuildInput input{};input.type=OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    input.instanceArray.instances=instance_data.address();input.instanceArray.numInstances=unsigned(instances.size());
    structures.push_back(acceleration(engine.context,input,handle));

      params.handle=handle;
      cuda_check(cudaMemsetAsync(accum.data,0,n*sizeof(pt::float4),stream.value),"clear frame accumulation");
      double frame_setup_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-frame_start).count();
    queued_params=rt::launch_plan(params,s.params,n,tile,passes);
    params=queued_params.back();
    // Every queued launch has its own immutable parameters: no upload/launch race.
    if(!legacy_sync) queued_parameters.upload(queued_params.data(),queued_params.size()*sizeof(params));
    size_t trace_launches=size_t(passes)*tiles;
    auto host_start=std::chrono::steady_clock::now();
    cuda_check(cudaEventRecord(trace_start.value,stream.value),"start trace timing");
    for(size_t slot=0;slot<queued_params.size();++slot) {
        if(slot==trace_launches) cuda_check(cudaEventRecord(trace_end.value,stream.value),"end trace timing");
        const auto &entry=queued_params[slot];
        CUdeviceptr address=queued_parameters.address()+slot*sizeof(params);
        if(legacy_sync) {parameters.upload(&entry,sizeof(entry));address=parameters.address();}
        const auto &active_sbt=slot<trace_launches?sbt:encode_sbt;
        ox(optixLaunch(engine.pipeline,reinterpret_cast<CUstream>(stream.value),address,sizeof(params),&active_sbt,entry.length,1,1),"OptiX launch");
        if(legacy_sync) cuda_check(cudaDeviceSynchronize(),"legacy tile completion");
    }
    cuda_check(cudaMemcpyAsync(image.data,bgra.data,n*sizeof(pt::uchar4),cudaMemcpyDeviceToHost,stream.value),"queue display readback");
    cuda_check(cudaStreamSynchronize(stream.value),"frame completion");
    float milliseconds=0;cuda_check(cudaEventElapsedTime(&milliseconds,trace_start.value,trace_end.value),"read trace timing");
    double seconds=milliseconds*.001;
    double render_wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-host_start).count();
    unsigned counts[2];cuda_check(cudaMemcpy(counts,counters.data,sizeof(counts),cudaMemcpyDeviceToHost),"read validation");
    if(false) {
        std::vector<pt::float4> linear(n);cuda_check(cudaMemcpy(linear.data(),accum.data,n*sizeof(pt::float4),cudaMemcpyDeviceToHost),"read linear");
        pt::write_linear(argv[6],linear,1.f/passes);
    }
    std::ostringstream report;
    report<<std::setprecision(10)<<"{\n \"backend\": \""<<(software?"OptiX raygen / software BVH":"OptiX 9 RT Core")<<"\",\n \"gpu\": \""<<prop.name<<"\",\n"
          <<" \"width\": "<<params.params.image.x<<",\n \"height\": "<<params.params.image.y<<",\n"
          <<" \"spp\": "<<uint64_t(params.params.image.z)*passes<<",\n \"depth\": "<<params.params.image.w<<",\n"
          <<" \"setup_seconds\": "<<frame_setup_seconds<<",\n \"render_seconds\": "<<seconds<<",\n"
          <<" \"render_wall_seconds\": "<<render_wall_seconds<<",\n \"launches\": "<<queued_params.size()<<",\n"
          <<" \"tile_synchronizations\": "<<(legacy_sync?queued_params.size():0)<<",\n"
          <<" \"transport\": \""<<(pipe?"pipe":shared?"mmap":"file")<<"\",\n"
          <<" \"verified_queries\": "<<counts[0]<<",\n \"query_mismatches\": "<<counts[1]<<",\n \"pure_path_tracing\": true\n}\n";
    if(pipe) rt::write_image(std::cout,image.data,params.params.image.x,params.params.image.y,report.str());
    else if(shared) {
        rt::MemoryOutput memory(mapped_image->data,mapped_image->size);std::ostream reply(&memory);
        rt::write_image(reply,image.data,params.params.image.x,params.params.image.y,report.str());
    }
    else {
        std::vector<pt::uchar4> display(image.data,image.data+n);
        pt::write_ppm(output_path.c_str(),display,params.params.image.x,params.params.image.y);
        std::ofstream file_report(output_path+".json");file_report<<report.str();file_report.close();
        if(!file_report) throw std::runtime_error("Report write failed");
    }
    log_out<<"Render "<<seconds<<" s; setup "<<frame_setup_seconds<<" s; checked "<<counts[0]<<" queries, "<<counts[1]<<" mismatches\n";
    if(!pipe) log_out<<"DONE\t"<<output_path<<std::endl;
    if(counts[1]) return 3;
    }
    return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}}
