// All full paths run in raygen. Only intersection queries enter the RT pipeline.
#include <cuda_runtime.h>
#include <optix_device.h>
#include "launch.h"
// LLVM NVPTX does not reliably lower external ext_vector_type return ABIs.
// Keep vector-returning integrator helpers within the OptiX entry programs.
#undef PT_FN
#undef PT_NOINLINE
#define PT_FN __host__ __device__ __forceinline__
#define PT_NOINLINE __host__ __device__ __noinline__ inline
#define RT_QUERY_FN __host__ __device__ __noinline__ inline
#include "pathtracer.h"
#include "hit_rules.h"
extern "C" { __constant__ rt::Launch launch; }
namespace rt {
struct Query { pt::Ray ray;float minimum,maximum,closest;unsigned layer;bool found;pt::Hit hit; };
__device__ __forceinline__ Query *query() {
    auto address=(static_cast<unsigned long long>(optixGetPayload_1())<<32)|optixGetPayload_0();
    return reinterpret_cast<Query*>(address);
}
__device__ __forceinline__ unsigned index() {
    return reinterpret_cast<const HitData*>(optixGetSbtDataPointer())->indices[optixGetPrimitiveIndex()];
}
__device__ __forceinline__ pt::Hit candidate(unsigned id) {
    const auto &g=launch.primitives[id];auto &q=*query();
    if(g.meta.x!=0) {
        auto uv=optixGetTriangleBarycentrics();
        return triangle_hit(g,q.ray,optixGetRayTmax(),uv.x,uv.y,id);
    }
    pt::Hit h{};h.t=optixGetRayTmax();h.p=q.ray.origin+h.t*q.ray.direction;
    h.normal=(h.p-g.a.xyz())/g.a.w;h.geometric=h.normal;
    h.front_face=pt::dot(q.ray.direction,h.normal)<0;h.bary=pt::f3(1,0,0);
    h.material=g.meta.y;h.primitive=id;return h;
}
}
namespace pt {
RT_QUERY_FN bool optix_world_hit(const Scene &s,Ray ray,float t_min,float t_max,Hit &rec,uint layer) {
#if defined(__CUDA_ARCH__)
    // Pure packages have only baked hulls. Layer 3 is an unused legacy guide query.
    if(launch.software || layer==3) return software_world_hit(s,ray,t_min,t_max,rec,layer);
    if(t_max<t_min) return false;
    rt::Query q{ray,t_min,t_max,t_max,layer,false,{}};
    auto pointer=reinterpret_cast<unsigned long long>(&q);
    unsigned lo=unsigned(pointer),hi=unsigned(pointer>>32);
    optixTrace(launch.handle,make_float3(ray.origin.x,ray.origin.y,ray.origin.z),
               make_float3(ray.direction.x,ray.direction.y,ray.direction.z),
               t_min,t_max,0.f,255,OPTIX_RAY_FLAG_NONE,0,1,0,lo,hi);
    if(q.found) rec=q.hit;
    if(launch.verify) {
        Hit ref{};bool found=software_world_hit(s,ray,t_min,t_max,ref,layer);
        atomicAdd(launch.errors,1u);
        bool mismatch=found!=q.found;
        if(found && q.found) {
            float tolerance=2e-4f*max(1.f,abs(ref.t));
            mismatch=abs(ref.t-q.hit.t)>tolerance || ref.material!=q.hit.material || ref.outline!=q.hit.outline ||
                     ref.front_face!=q.hit.front_face || ref.has_uv!=q.hit.has_uv ||
                     (ref.has_uv && length(ref.uv-q.hit.uv)>2e-4f) || dot(ref.normal,q.hit.normal)<0.999f;
        }
        if(mismatch) atomicAdd(launch.errors+1,1u);
    }
    return q.found;
#else
    return software_world_hit(s,ray,t_min,t_max,rec,layer);
#endif
}
}
extern "C" __global__ void __anyhit__filter() {
    auto &q=*rt::query();unsigned id=rt::index();auto p=launch.params;
    pt::Scene s{launch.primitives,launch.nodes,launch.materials,launch.pixels,&p,nullptr};
    if(!rt::eligible(s,id,q.layer)) {optixIgnoreIntersection();return;}
    auto h=rt::candidate(id);
    // Preserve the original near-parallel triangle threshold.
    const auto &g=launch.primitives[id];
    if(g.meta.x && pt::abs(pt::dot(g.b.xyz()-g.a.xyz(),pt::cross(q.ray.direction,g.c.xyz()-g.a.xyz())))<1e-8f) {
        optixIgnoreIntersection();return;
    }
    if(!rt::accept(s,id,q.ray,q.minimum,q.closest,h)) {optixIgnoreIntersection();return;}
    // Alpha is evaluated in the original ordered nearest-hit loop, never here.
    q.closest=pt::min(q.closest,h.t);
}
extern "C" __global__ void __closesthit__surface() {
    auto &q=*rt::query();q.hit=rt::candidate(rt::index());
    q.hit.outline=launch.primitives[q.hit.primitive].meta.x==2;q.found=true;
}
extern "C" __global__ void __intersection__sphere() {
    pt::Hit h{};auto &q=*rt::query();
    if(pt::primitive_hit(launch.primitives[rt::index()],q.ray,optixGetRayTmin(),optixGetRayTmax(),h))
        optixReportIntersection(h.t,0);
}
extern "C" __global__ void __miss__empty() {}
extern "C" __global__ void __raygen__trace() {
    unsigned offset=optixGetLaunchIndex().x;if(offset>=launch.length) return;
    unsigned i=launch.base+offset;auto p=launch.params;
    pt::render_kernel(launch.primitives,launch.nodes,launch.materials,launch.pixels,
                      launch.rng,p,launch.frame,launch.accum,nullptr,pt::u2(i%p.image.x,i/p.image.x));
}
extern "C" __global__ void __raygen__encode() {
    unsigned offset=optixGetLaunchIndex().x;if(offset>=launch.length) return;
    unsigned i=launch.base+offset;auto p=launch.params;
    pt::display_kernel(launch.accum,launch.bgra,p,nullptr,launch.materials,nullptr,pt::u2(i%p.image.x,i/p.image.x));
}
