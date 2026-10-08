#include "reconstruction.h"
#include <optix_stubs.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_helpers_dlssd_cuda.h>
#include <cuda.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <unistd.h>

namespace reconstruction {
static void ck(cudaError_t r) { if(r!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(r)); }
static void ox(OptixResult r) { if(r!=OPTIX_SUCCESS) throw std::runtime_error(std::string("OptiX denoiser: ")+optixGetErrorName(r)); }
static void ngx(NVSDK_NGX_Result r,const char *operation) {
    if(NVSDK_NGX_FAILED(r)) throw std::runtime_error(std::string(operation)+": NGX result "+std::to_string(unsigned(r)));
}
struct Device {
    void *p=nullptr;
    explicit Device(size_t bytes=0) { if(bytes) ck(cudaMalloc(&p,bytes)); }
    ~Device() { if(p) cudaFree(p); }
    Device(const Device&)=delete;
    Device& operator=(const Device&)=delete;
    CUdeviceptr address() const { return reinterpret_cast<CUdeviceptr>(p); }
};
// NGX CUDA consumes CUDA texture/surface objects, not arbitrary linear pointers.
struct Texture {
    cudaArray_t array=nullptr;
    cudaTextureObject_t texture=0;
    cudaSurfaceObject_t surface=0;
    unsigned width,height,channels;
    Texture(unsigned w,unsigned h,unsigned c):width(w),height(h),channels(c) {
        auto desc=c==1?cudaCreateChannelDesc<float>():c==2?cudaCreateChannelDesc<float2>():cudaCreateChannelDesc<float4>();
        ck(cudaMallocArray(&array,&desc,w,h,cudaArraySurfaceLoadStore));
        cudaResourceDesc resource{};resource.resType=cudaResourceTypeArray;resource.res.array.array=array;
        cudaTextureDesc options{};options.readMode=cudaReadModeElementType;
        options.filterMode=cudaFilterModePoint;options.addressMode[0]=options.addressMode[1]=cudaAddressModeClamp;
        ck(cudaCreateTextureObject(&texture,&resource,&options,nullptr));
        ck(cudaCreateSurfaceObject(&surface,&resource));
    }
    void upload(const void *p,cudaStream_t stream) {
        auto row=size_t(width)*channels*sizeof(float);
        ck(cudaMemcpy2DToArrayAsync(array,0,0,p,row,row,height,cudaMemcpyDeviceToDevice,stream));
    }
    void download(void *p,cudaStream_t stream) {
        auto row=size_t(width)*channels*sizeof(float);
        ck(cudaMemcpy2DFromArrayAsync(p,row,array,0,0,row,height,cudaMemcpyDeviceToDevice,stream));
    }
    ~Texture() { if(texture)cudaDestroyTextureObject(texture);if(surface)cudaDestroySurfaceObject(surface);if(array)cudaFreeArray(array); }
};
__global__ void normalize_accum(const float4 *source,float4 *target,size_t n,float scale) {
    size_t i=size_t(blockIdx.x)*blockDim.x+threadIdx.x;if(i>=n)return;
    float4 a=source[i];target[i]=make_float4(a.x*scale,a.y*scale,a.z*scale,1);
}
__global__ void camera_normals(const float4 *source,float4 *target,size_t n,
                               float3 right,float3 up,float3 back) {
    size_t i=size_t(blockIdx.x)*blockDim.x+threadIdx.x;if(i>=n)return;
    auto a=source[i];target[i]=make_float4(a.x*right.x+a.y*right.y+a.z*right.z,
        a.x*up.x+a.y*up.y+a.z*up.z,a.x*back.x+a.y*back.y+a.z*back.z,0);
}
__global__ void pack_flow(const float4 *source,float2 *target,size_t n) {
    size_t i=size_t(blockIdx.x)*blockDim.x+threadIdx.x;if(i<n)target[i]=make_float2(source[i].x,source[i].y);
}
struct RandomState { uint4 state,extra; };
static_assert(sizeof(RandomState)==32);
__global__ void initialize_random(RandomState *states,size_t n,unsigned frame_seed) {
    size_t i=size_t(blockIdx.x)*blockDim.x+threadIdx.x;if(i>=n)return;
    // Same per-pixel seed expansion as the existing integrator, with a separate
    // frame seed so DLSS does not receive the same noise pattern each frame.
    unsigned x=1984+unsigned(i)+frame_seed*0x9e3779b9u;RandomState r;
    auto values=reinterpret_cast<unsigned*>(&r.state);
    for(unsigned j=0;j<4;++j){x+=0x9e3779b9u;unsigned z=x;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;values[j]=z^(z>>16);}
    r.extra=make_uint4(x^0xa511e9b3u,362437u,0,0);states[i]=r;
}
void prepare_random(void *rng,size_t n,unsigned frame_seed,cudaStream_t stream) {
    initialize_random<<<unsigned((n+255)/256),256,0,stream>>>(static_cast<RandomState*>(rng),n,frame_seed);ck(cudaGetLastError());
}
static std::wstring executable_directory() {
    char buffer[4096];auto n=readlink("/proc/self/exe",buffer,sizeof(buffer)-1);
    if(n<0)throw std::runtime_error("Cannot resolve executable directory");buffer[n]=0;
    return std::filesystem::path(buffer).parent_path().wstring();
}
static void init_ngx() {
    // This identifies this renderer; it is not an NVIDIA-issued application ID.
    auto dir=executable_directory();
    ngx(NVSDK_NGX_CUDA_Init_with_ProjectID("054adfc1-c9a5-4d8a-94b2-dc0f88244237",
        NVSDK_NGX_ENGINE_TYPE_CUSTOM,"1.0.0",dir.c_str()),"NGX CUDA initialization");
}
void probe_dlss() {
    ck(cudaSetDevice(0));ck(cudaFree(nullptr));init_ngx();
    NVSDK_NGX_Parameter *params=nullptr;
    ngx(NVSDK_NGX_CUDA_GetCapabilityParameters(&params),"NGX capabilities");
    int available=0;auto result=params->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,&available);
    std::cout<<"DLSS_RR_CAPABILITY_RESULT "<<unsigned(result)<<" AVAILABLE "<<available<<'\n';
    NVSDK_NGX_CUDA_DestroyParameters(params);NVSDK_NGX_CUDA_Shutdown();
    if(NVSDK_NGX_FAILED(result)||!available)throw std::runtime_error("DLSS Ray Reconstruction unavailable on this driver/runtime");
}
void optimal_dlss(unsigned width,unsigned height) {
    ck(cudaSetDevice(0));ck(cudaFree(nullptr));init_ngx();
    NVSDK_NGX_Parameter *params=nullptr;
    try {
        ngx(NVSDK_NGX_CUDA_GetCapabilityParameters(&params),"NGX capabilities");
        unsigned w=0,h=0,minw=0,minh=0,maxw=0,maxh=0;float sharpness=0;
        ngx(NGX_DLSSD_GET_OPTIMAL_SETTINGS(params,width,height,NVSDK_NGX_PerfQuality_Value_MaxQuality,
            &w,&h,&minw,&minh,&maxw,&maxh,&sharpness),"DLSS optimal settings");
        std::cout<<"{\"mode\":\"quality\",\"input_width\":"<<w<<",\"input_height\":"<<h
            <<",\"output_width\":"<<width<<",\"output_height\":"<<height<<"}\n";
    } catch(...) {
        if(params)NVSDK_NGX_CUDA_DestroyParameters(params);NVSDK_NGX_CUDA_Shutdown();throw;
    }
    NVSDK_NGX_CUDA_DestroyParameters(params);NVSDK_NGX_CUDA_Shutdown();
}
struct Processor::Impl {
    std::string selected;unsigned w,h,ow,oh;cudaStream_t stream;
    Device normalized,output,normals,flow,state,scratch,intensity;
    OptixDenoiser denoiser=nullptr;
    NVSDK_NGX_Parameter *params=nullptr;NVSDK_NGX_Handle *handle=nullptr;bool ngx_ready=false;
    std::vector<std::unique_ptr<Texture>> textures;
    Impl(OptixDeviceContext context,std::string mode,unsigned width,unsigned height,
         unsigned output_width,unsigned output_height,cudaStream_t str):
        selected(mode),w(width),h(height),ow(output_width),oh(output_height),stream(str),
        normalized(size_t(w)*h*16),output(size_t(ow)*oh*16),normals(size_t(w)*h*16),
        flow(size_t(w)*h*8),intensity(4) {
        try { if(selected=="optix") {
            if(w!=ow||h!=oh)throw std::runtime_error("OptiX spatial denoising preserves resolution; use DLSS for upscaling");
            OptixDenoiserOptions options{};options.guideAlbedo=1;options.guideNormal=1;
            ox(optixDenoiserCreate(context,OPTIX_DENOISER_MODEL_KIND_HDR,&options,&denoiser));
            OptixDenoiserSizes sizes{};ox(optixDenoiserComputeMemoryResources(denoiser,w,h,&sizes));
            ck(cudaMalloc(&state.p,sizes.stateSizeInBytes));ck(cudaMalloc(&scratch.p,sizes.withoutOverlapScratchSizeInBytes));
            state_bytes=sizes.stateSizeInBytes;scratch_bytes=sizes.withoutOverlapScratchSizeInBytes;
            ox(optixDenoiserSetup(denoiser,reinterpret_cast<CUstream>(stream),w,h,state.address(),state_bytes,scratch.address(),scratch_bytes));
        } else if(selected=="dlss") {
            init_ngx();ngx_ready=true;ngx(NVSDK_NGX_CUDA_GetCapabilityParameters(&params),"NGX capabilities");
            int available=0;ngx(params->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,&available),"DLSS RR availability");
            if(!available)throw std::runtime_error("DLSS RR unsupported by installed driver/runtime");
            for(unsigned c:{4u,4u,4u,4u,1u,1u,2u})textures.emplace_back(new Texture(w,h,c));
            textures.emplace_back(new Texture(ow,oh,4));
            NVSDK_NGX_CUDA_DLSSD_Create_Params create{};
            create.Feature.InWidth=w;create.Feature.InHeight=h;create.Feature.InTargetWidth=ow;create.Feature.InTargetHeight=oh;
            create.Feature.InPerfQualityValue=w==ow&&h==oh?NVSDK_NGX_PerfQuality_Value_DLAA:NVSDK_NGX_PerfQuality_Value_MaxQuality;
            create.Feature.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
            create.Feature.InDenoiseMode=NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
            create.Feature.InUseHWDepth=NVSDK_NGX_DLSS_Depth_Type_Linear;
            create.Feature.InRoughnessMode=NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;
            CUcontext cuda_context=nullptr;cuCtxGetCurrent(&cuda_context);create.InCUContext=cuda_context;create.InCUStream=stream;
            ngx(NGX_CUDA_CREATE_DLSSD_EXT(&handle,params,&create),"Create CUDA DLSS RR feature");
        } else throw std::runtime_error("Reconstruction must be optix or dlss");
        } catch(...) {cleanup();throw;}
    }
    size_t state_bytes=0,scratch_bytes=0;
    OptixImage2D image(void *p,OptixPixelFormat format,unsigned bytes) {
        OptixImage2D value{};value.data=reinterpret_cast<CUdeviceptr>(p);value.width=w;value.height=h;
        value.rowStrideInBytes=w*bytes;value.pixelStrideInBytes=bytes;value.format=format;return value;
    }
    void *run(const Frame &f) {
        size_t n=size_t(w)*h;
        normalize_accum<<<unsigned((n+255)/256),256,0,stream>>>(static_cast<float4*>(f.accum),static_cast<float4*>(normalized.p),n,1.f/f.passes);ck(cudaGetLastError());
        if(selected=="optix") {
            float3 right=make_float3(f.world_to_view[0],f.world_to_view[1],f.world_to_view[2]);
            float3 up=make_float3(f.world_to_view[4],f.world_to_view[5],f.world_to_view[6]);
            float3 back=make_float3(f.world_to_view[8],f.world_to_view[9],f.world_to_view[10]);
            camera_normals<<<unsigned((n+255)/256),256,0,stream>>>(static_cast<float4*>(f.normal),static_cast<float4*>(normals.p),n,right,up,back);ck(cudaGetLastError());
            auto input=image(normalized.p,OPTIX_PIXEL_FORMAT_FLOAT4,16);
            ox(optixDenoiserComputeIntensity(denoiser,reinterpret_cast<CUstream>(stream),&input,intensity.address(),scratch.address(),scratch_bytes));
            OptixDenoiserGuideLayer guides{};guides.albedo=image(f.albedo,OPTIX_PIXEL_FORMAT_FLOAT4,16);guides.normal=image(normals.p,OPTIX_PIXEL_FORMAT_FLOAT4,16);
            OptixDenoiserLayer layer{};layer.input=input;layer.output=image(output.p,OPTIX_PIXEL_FORMAT_FLOAT4,16);
            OptixDenoiserParams parameters{};parameters.hdrIntensity=intensity.address();parameters.blendFactor=0;
            ox(optixDenoiserInvoke(denoiser,reinterpret_cast<CUstream>(stream),&parameters,state.address(),state_bytes,&guides,&layer,1,0,0,scratch.address(),scratch_bytes));
        } else {
            pack_flow<<<unsigned((n+255)/256),256,0,stream>>>(static_cast<float4*>(f.motion),static_cast<float2*>(flow.p),n);ck(cudaGetLastError());
            void *inputs[]={normalized.p,f.albedo,f.specular,f.normal,f.roughness,f.depth,flow.p};
            for(unsigned i=0;i<7;++i)textures[i]->upload(inputs[i],stream);
            NVSDK_NGX_CUDA_DLSSD_Eval_Params eval{};
            eval.pInColor=&textures[0]->texture;eval.pInDiffuseAlbedo=&textures[1]->texture;
            eval.pInSpecularAlbedo=&textures[2]->texture;eval.pInNormals=&textures[3]->texture;
            eval.pInRoughness=&textures[4]->texture;eval.pInDepth=&textures[5]->texture;
            eval.pInMotionVectors=&textures[6]->texture;eval.pInOutput=&textures[7]->surface;
            eval.pInWorldToViewMatrix=const_cast<float*>(f.world_to_view);eval.pInViewToClipMatrix=const_cast<float*>(f.view_to_clip);
            eval.InJitterOffsetX=f.jitter_x;eval.InJitterOffsetY=f.jitter_y;
            eval.InIndicatorInvertXAxis=0;eval.InIndicatorInvertYAxis=1;
            eval.InMVScaleX=1;eval.InMVScaleY=1;eval.InReset=f.reset;eval.InRenderSubrectDimensions={w,h};
            eval.InPreExposure=1;eval.InExposureScale=1;eval.InFrameTimeDeltaInMsec=f.delta_ms;
            // NGX may submit work on its own internal CUDA streams. An input
            // texture upload or output readback must not race that submission.
            ck(cudaStreamSynchronize(stream));
            ngx(NGX_CUDA_EVALUATE_DLSSD_EXT(handle,params,&eval),"Evaluate CUDA DLSS RR");
            ck(cudaDeviceSynchronize());
            textures[7]->download(output.p,stream);
        }
        return output.p;
    }
    void cleanup() noexcept {
        cudaStreamSynchronize(stream);
        if(handle)NVSDK_NGX_CUDA_ReleaseFeature(handle);
        if(params)NVSDK_NGX_CUDA_DestroyParameters(params);
        if(ngx_ready)NVSDK_NGX_CUDA_Shutdown();
        if(denoiser)optixDenoiserDestroy(denoiser);
    }
    ~Impl() {cleanup();}
};
Processor::Processor(OptixDeviceContext c,std::string m,unsigned w,unsigned h,unsigned ow,unsigned oh,cudaStream_t s):impl(new Impl(c,m,w,h,ow,oh,s)){}
Processor::~Processor()=default;
void *Processor::run(const Frame &f){return impl->run(f);}
const char *Processor::mode()const{return impl->selected.c_str();}
} // namespace reconstruction
