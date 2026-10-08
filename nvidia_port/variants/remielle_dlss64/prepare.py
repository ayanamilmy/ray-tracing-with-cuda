#!/usr/bin/env python3
"""Generate a separate backend from pinned existing files; never edit originals."""
from pathlib import Path
import argparse
import shutil
import hashlib
import json
import re

parser=argparse.ArgumentParser()
parser.add_argument('--port-root',type=Path,default=Path(__file__).resolve().parent.parent)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
root=args.port_root.resolve();out=args.output.resolve();here=Path(__file__).resolve().parent
if out==root or root in out.parents and 'reconstruction' not in out.parts:
    raise SystemExit('Output must be outside the original port or inside reconstruction/')
sources={}
def copy(relative):
    src=root/relative;dst=out/relative
    dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
    sources[relative]=hashlib.sha256(src.read_bytes()).hexdigest()
for name in ('types.h','vector_math.h','scene_io.h'):
    copy('include/'+name)
for name in ('render_sequence.cpp','launch.h','geometry.h','frame_transport.h','launch_plan.h','mapped_transport.h'):
    copy('optix/'+name)
for name in ('device.cu','launch.h','types.h','vector_math.h','pathtracer.h','hit_rules.h'):
    copy('build/optix/nvcc/'+name)
for name in ('guides.cuh','camera.h','reconstruction.h','reconstruction.cu'):
    shutil.copy2(here/name,out/name)
guide=(out/'guides.cuh').read_text()
guide=re.sub(r'\.(xyz|xy)\b',r'.\1()',guide)
guide=guide.replace('float2 uv','pt::float2 uv').replace('float3 previous','pt::float3 previous').replace('max(', 'pt::max(')
(out/'guides.cuh').write_text(guide)

def replace(s,old,new):
    if s.count(old)!=1:raise RuntimeError('Source changed; patch anchor not unique: '+old[:90])
    return s.replace(old,new)
extra='''
    pt::float4 *guide_albedo,*guide_normal,*guide_motion,*guide_specular;
    float *guide_depth,*guide_roughness;
    const pt::GPUPrimitive *previous_primitives;
    pt::GPUParams nominal_camera,previous_camera;
    unsigned guide_reset;
'''
for path in (out/'optix/launch.h',out/'build/optix/nvcc/launch.h'):
    s=path.read_text();s=replace(s,'unsigned *errors;','unsigned *errors;'+extra)
    s=replace(s,'static_assert(sizeof(Launch)==624 && alignof(Launch)==16);','static_assert(alignof(Launch)==16);')
    path.write_text(s)
p=out/'build/optix/nvcc/device.cu';p.write_text(p.read_text()+'\n#include "guides.cuh"\n')
p=out/'optix/render_sequence.cpp';s=p.read_text()
s=replace(s,'int main(int argc,char **argv) {try {','''int main(int argc,char **argv) {try {
    if(argc==4&&std::string(argv[1])=="--dlss-optimal-size") {
        reconstruction::optimal_dlss(pt::positive_arg(argv[2]),pt::positive_arg(argv[3]));return 0;
    }''')
s=replace(s,'#include "mapped_transport.h"','#include "mapped_transport.h"\n#include "reconstruction.h"\n#include "camera.h"')
s=replace(s,'auto s=pt::load_scene(argv[2]);auto geom=rt::geometry(s);','''auto s=pt::load_scene(argv[2]);auto geom=rt::geometry(s);
    const std::string reconstruction_mode=std::getenv("NPT_RECONSTRUCTION")?std::getenv("NPT_RECONSTRUCTION"):"off";
    const bool reconstruct=reconstruction_mode!="off";
    auto dimension=[](const char *key,unsigned fallback){return std::getenv(key)?pt::positive_arg(std::getenv(key)):fallback;};
    const unsigned output_width=dimension("NPT_OUTPUT_WIDTH",s.params.image.x);
    const unsigned output_height=dimension("NPT_OUTPUT_HEIGHT",s.params.image.y);
    if(!reconstruct&&(output_width!=s.params.image.x||output_height!=s.params.image.y))throw std::runtime_error("Upscaling requires DLSS");
    const size_t output_pixels=size_t(output_width)*output_height;
    const float delta_ms=std::getenv("NPT_FRAME_DELTA_MS")?std::stof(std::getenv("NPT_FRAME_DELTA_MS")):1000.f/120;
    if(!(delta_ms>0))throw std::runtime_error("Frame delta must be positive");
    unsigned history_frame=0;bool reset_history=true;
    auto previous_scene_primitives=s.primitives;auto previous_camera=s.params;''')
s=replace(s,'desc.raygen.entryFunctionName="__raygen__encode";auto encode=engine.group(desc);','desc.raygen.entryFunctionName="__raygen__encode";auto encode=engine.group(desc);\n    desc.raygen.entryFunctionName="__raygen__guides";auto guide_group=engine.group(desc);')
s=replace(s,'Record<Empty> rg_trace{},rg_encode{},ms{};','Record<Empty> rg_trace{},rg_encode{},rg_guides{},ms{};\n    ox(optixSbtRecordPackHeader(guide_group,&rg_guides),"pack guides");')
s=replace(s,'bgra(n*sizeof(pt::uchar4))','bgra(output_pixels*sizeof(pt::uchar4))')
s=replace(s,'HostImage image(n);\n    Stream stream;','''HostImage image(output_pixels);
    Buffer albedo(reconstruct?n*16:0),normal(reconstruct?n*16:0),motion(reconstruct?n*16:0),specular(reconstruct?n*16:0);
    Buffer depth(reconstruct?n*4:0),roughness(reconstruct?n*4:0),previous_primitives(reconstruct?s.primitives.size()*sizeof(pt::GPUPrimitive):0);
    Buffer reconstructed_parameters(sizeof(rt::Launch));
    Buffer guide_record(sizeof(rg_guides));guide_record.upload(&rg_guides,sizeof(rg_guides));
    auto guide_sbt=sbt;guide_sbt.raygenRecord=guide_record.address();
    Stream stream;
    std::unique_ptr<reconstruction::Processor> processor;
    if(reconstruct)processor=std::make_unique<reconstruction::Processor>(engine.context,reconstruction_mode,unsigned(s.params.image.x),unsigned(s.params.image.y),output_width,output_height,stream.value);''')
s=replace(s,'params.verify=verify;params.software=software;params.errors=counters.as<unsigned>();','''params.verify=verify;params.software=software;params.errors=counters.as<unsigned>();
    params.guide_albedo=albedo.as<pt::float4>();params.guide_normal=normal.as<pt::float4>();
    params.guide_motion=motion.as<pt::float4>();params.guide_specular=specular.as<pt::float4>();
    params.guide_depth=depth.as<float>();params.guide_roughness=roughness.as<float>();
    params.previous_primitives=previous_primitives.as<pt::GPUPrimitive>();''')
s=replace(s,'packet_path=command.substr(0,split);output_path=command.substr(split+1);','''packet_path=command.substr(0,split);output_path=command.substr(split+1);
        auto control=output_path.find('\t');
        if(control!=std::string::npos){
            if(output_path.substr(control+1)!="RESET")throw std::runtime_error("Unknown frame control; expected RESET");
            output_path.resize(control);reset_history=true;
        }''')
s=replace(s,'20+n*sizeof(pt::uchar4)+1048576','20+output_pixels*sizeof(pt::uchar4)+1048576')
s=replace(s,'queued_params=rt::launch_plan(params,s.params,n,tile,passes);','''reconstruction::Frame reconstruction_frame;
    reconstruction_frame.accum=accum.data;reconstruction_frame.albedo=albedo.data;reconstruction_frame.normal=normal.data;
    reconstruction_frame.motion=motion.data;reconstruction_frame.specular=specular.data;
    reconstruction_frame.depth=depth.data;reconstruction_frame.roughness=roughness.data;
    reconstruction_frame.passes=passes;reconstruction_frame.reset=reset_history;reconstruction_frame.delta_ms=delta_ms;
    reconstruction::camera(reconstruction_frame,s.params);
    auto render_camera=s.params;
    if(reconstruct){
        previous_primitives.upload(previous_scene_primitives.data(),previous_scene_primitives.size()*sizeof(pt::GPUPrimitive));
        params.previous_camera=previous_camera;params.nominal_camera=s.params;params.guide_reset=reset_history;
        if(reconstruction_mode=="dlss"){
            float jx=reconstruction::halton(history_frame%1024+1,2)-.5f;
            float jy=reconstruction::halton(history_frame%1024+1,3)-.5f;
            render_camera.lower_left+=render_camera.horizontal*(jx/render_camera.image.x)+render_camera.vertical*(jy/render_camera.image.y);
            reconstruction_frame.jitter_x=-jx;reconstruction_frame.jitter_y=-jy;
        }
    }
    queued_params=rt::launch_plan(params,render_camera,n,tile,passes);
    if(reconstruction_mode=="dlss"){
        reconstruction::prepare_random(rng.data,n,history_frame+1,stream.value);
        for(size_t q=0;q<tiles;++q)queued_params[q].params.counts.w=1;
    }''')
s=replace(s,'for(size_t slot=0;slot<queued_params.size();++slot) {','for(size_t slot=0;slot<(reconstruct?trace_launches:queued_params.size());++slot) {')
s=replace(s,'cuda_check(cudaMemcpyAsync(image.data,bgra.data,n*sizeof(pt::uchar4),cudaMemcpyDeviceToHost,stream.value),"queue display readback");','''double reconstruction_seconds=0;
    if(reconstruct){
        cuda_check(cudaEventRecord(trace_end.value,stream.value),"end trace timing");
        Event reconstruct_begin,reconstruct_end;
        cuda_check(cudaEventRecord(reconstruct_begin.value,stream.value),"start reconstruction timing");
        parameters.upload(&params,sizeof(params));
        ox(optixLaunch(engine.pipeline,reinterpret_cast<CUstream>(stream.value),parameters.address(),sizeof(params),&guide_sbt,unsigned(n),1,1),"guide launch");
        auto processed=processor->run(reconstruction_frame);
        cuda_check(cudaEventRecord(reconstruct_end.value,stream.value),"end reconstruction timing");
        params.accum=static_cast<pt::float4*>(processed);params.params.image.x=output_width;params.params.image.y=output_height;params.params.filter.z=1;
        params.base=0;params.length=unsigned(output_pixels);reconstructed_parameters.upload(&params,sizeof(params));
        ox(optixLaunch(engine.pipeline,reinterpret_cast<CUstream>(stream.value),reconstructed_parameters.address(),sizeof(params),&encode_sbt,unsigned(output_pixels),1,1),"reconstructed display");
        cuda_check(cudaEventSynchronize(reconstruct_end.value),"reconstruction completion");
        float ms=0;cuda_check(cudaEventElapsedTime(&ms,reconstruct_begin.value,reconstruct_end.value),"reconstruction timing");reconstruction_seconds=ms*.001;
        params.accum=accum.as<pt::float4>();
        previous_scene_primitives=s.primitives;previous_camera=s.params;reset_history=false;++history_frame;
    }
    cuda_check(cudaMemcpyAsync(image.data,bgra.data,output_pixels*sizeof(pt::uchar4),cudaMemcpyDeviceToHost,stream.value),"queue display readback");''')
s=replace(s,'report<<std::setprecision(10)<<',r'''report<<std::setprecision(10)<<"{\n \"reconstruction\": \""<<reconstruction_mode<<"\",\n \"reconstruction_seconds\": "<<reconstruction_seconds
          <<",\n \"input_width\": "<<s.params.image.x<<",\n \"input_height\": "<<s.params.image.y<<",\n \"path_traced_lighting\": true,\n \"unprocessed_path_tracing\": "<<(reconstruct?"false":"true")<<",\n";
    report<<''')
s=replace(s,r'"{\n \"backend',r'" \"backend')
s=replace(s,'image.data+n);','image.data+output_pixels);')
s=s.replace('<<queued_params.size()<<', '<<(reconstruct?trace_launches+2:queued_params.size())<<')
p.write_text(s)
(out/'source-manifest.json').write_text(json.dumps(sources,indent=2)+'\n')
print(out)
