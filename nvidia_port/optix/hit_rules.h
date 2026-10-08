#pragma once
// Included after the generated integrator: use its Hit/Ray and math types.
namespace rt {
PT_FN pt::Hit triangle_hit(const pt::GPUPrimitive &g, pt::Ray ray,
                           float t, float u, float v, unsigned index) {
    using namespace pt;
    Hit h{}; float w = 1-u-v;
    h.t=t; h.p=ray.origin+t*ray.direction; h.bary=f3(w,u,v);
    h.material=g.meta.y; h.primitive=index; h.has_uv=(g.meta.z&1u)!=0;
    h.uv=h.has_uv ? w*g.uv01.xy+u*g.uv01.zw+v*g.uv2.xy : f2(0);
    pt::float3 ng=normalize(cross(g.b.xyz-g.a.xyz,g.c.xyz-g.a.xyz)), ns=ng;
    float winding=(g.meta.z&2u) && dot(ng,g.n0.xyz+g.n1.xyz+g.n2.xyz)<0 ? -1.f : 1.f;
    h.front_face=dot(ray.direction,ng)*winding<0;
    if(g.meta.z&2u) {
        pt::float3 interpolated=w*g.n0.xyz+u*g.n1.xyz+v*g.n2.xyz;
        if(dot(interpolated,interpolated)>1e-20f) ns=normalize(interpolated);
        if(dot(ns,ng)<0) ns=-ns;
    }
    if(dot(ray.direction,ng)>0) {ng=-ng;ns=-ns;}
    if(dot(ray.direction,ns)>=0) ns=ng;
    h.normal=ns;h.geometric=ng;return h;
}
PT_FN bool eligible(const pt::Scene &s,unsigned index,unsigned layer) {
    const auto &g=s.primitives[index];const auto &m=s.materials[g.meta.y];
    bool overlay=m.flags.x==4 && m.mmd.w>0;
    if(g.meta.x==2 && (!(s.camera->features.x&2u) || (layer!=0 && layer!=2))) return false;
    if((layer==1)!=overlay) return false;
    if(layer==4 && (m.zzz_misc.w<=0 || unsigned(m.zzz_misc.y)!=4 || !m.mmd_flags.z)) return false;
    if(layer==2 && m.flags.x==3 && !m.flags.w) return false;
    if(layer==2 && m.flags.x==4 && !m.mmd_flags.z) return false;
    if(layer==5 && (m.zzz_face_detail.w<=0 || m.zzz_render.w<1 || m.zzz_render.w>2)) return false;
    return true;
}
PT_FN bool accept(const pt::Scene &s,unsigned index,pt::Ray ray,float t_min,
                 float current_max,pt::Hit &h) {
    using namespace pt;
    const auto &g=s.primitives[index];const auto &m=s.materials[g.meta.y];
    if(g.meta.x==2) {
        float sign=(g.meta.z&4u) ? -1.f : 1.f;Hit original;
        if(dot(cross(g.b.xyz-g.a.xyz,g.c.xyz-g.a.xyz),ray.direction)*sign<=0 ||
           primitive_hit(s.primitives[g.meta.w>>8],ray,t_min,current_max,original)) return false;
        h.outline=true;
    } else if(m.zzz_face_detail.w>0 &&
              ((m.zzz_render.z==1 && h.front_face) || (m.zzz_render.z==2 && !h.front_face))) return false;
    return true;
}
}
