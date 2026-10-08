// Generated from snapshot/pathtracer.metal by tools/translate.py; edit the snapshot or translator, then regenerate.
#pragma once

#include "types.h"
#ifndef RT_QUERY_FN
#define RT_QUERY_FN PT_NOINLINE
#endif
namespace pt {
constexpr float PI = 3.14159265358979323846f;
constexpr float3 LIGHT_CENTER = f3(0, 5, 0);
constexpr float LIGHT_RADIUS = 1.0f;
constexpr float3 LIGHT_EMISSION = f3(4);

struct Ray { float3 origin, direction; };
struct Hit { float t; float3 p, normal, geometric, bary; float2 uv; uint material, primitive; bool has_uv, front_face, outline; };
struct Scene {
    const GPUPrimitive *primitives;
    const GPUNode *nodes;
    const GPUMaterial *materials;
    const uchar *pixels;
    GPUParams *camera;
    const float *depths;
};

// A per-pixel XORWOW generator; no global RNG or cross-atomics.
// State initialization is backend-local, while all sampling distributions match CUDA.
PT_FN GPURandom seed_random(uint seed)
{
    uint x = seed;
    GPURandom r;
    for (uint i = 0; i < 4; ++i) {
        x += 0x9e3779b9u;
        uint z = x; z = (z ^ (z >> 16)) * 0x85ebca6bu;
        z = (z ^ (z >> 13)) * 0xc2b2ae35u;
        r.state[i] = z ^ (z >> 16);
    }
    r.extra = u4(x ^ 0xa511e9b3u, 362437u, 0, 0);
    return r;
}
PT_FN float random_uniform(GPURandom &r)
{
    uint t = r.state.x ^ (r.state.x >> 2);
    r.state = u4(r.state.y, r.state.z, r.state.w, r.extra.x);
    r.extra.x ^= (r.extra.x << 4) ^ t ^ (t << 1);
    r.extra.y += 362437u;
    return (float(r.extra.x + r.extra.y) + 0.5f) * 0x1p-32f;
}
PT_FN float3 random_ball(GPURandom &r)
{
    float3 p;
    do {
        float x = random_uniform(r), y = random_uniform(r), z = random_uniform(r);
        p = 2.0f * f3(x, y, z) - 1.0f;
    } while (dot(p, p) >= 1.0f);
    return p;
}
PT_FN float3 random_unit(GPURandom &r)
{
    float3 p;
    do { p = random_ball(r); } while (dot(p, p) <= 1e-12f);
    return normalize(p);
}
PT_FN float2 random_disk(GPURandom &r)
{
    float2 p;
    do {
        float x = random_uniform(r), y = random_uniform(r);
        p = 2.0f * f2(x, y) - 1.0f;
    } while (dot(p, p) >= 1.0f);
    return p;
}
PT_FN bool box_hit(const GPUNode &n, Ray ray, float t_min, float t_max, float &entry, float padding=0)
{
    for (uint a = 0; a < 3; ++a) {
        if (ray.direction[a] == 0.0f) {
            if (ray.origin[a] < n.lo[a]-padding || ray.origin[a] > n.hi[a]+padding) return false;
            continue;
        }
        float inv = 1.0f / ray.direction[a];
        float t0 = (n.lo[a]-padding - ray.origin[a]) * inv;
        float t1 = (n.hi[a]+padding - ray.origin[a]) * inv;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        t_min = max(t_min, t0); t_max = min(t_max, t1);
        if (t_max <= t_min) return false;
    }
    entry = t_min;
    return true;
}
PT_FN bool primitive_hit(const GPUPrimitive &g, Ray ray, float t_min,
                          float t_max, Hit &rec)
{
    rec.outline=false;
    if (g.meta.x == 0) {
        float3 oc = ray.origin - g.a.xyz();
        float a = dot(ray.direction, ray.direction);
        float b = 2.0f * dot(oc, ray.direction);
        float c = dot(oc, oc) - g.a.w*g.a.w;
        float d = b*b - 4*a*c;
        if (d <= 0) return false;
        float root = sqrt(d);
        float t = (-b-root)/(2*a);
        if (t < t_min || t > t_max) {
            t = (-b+root)/(2*a);
            if (t < t_min || t > t_max) return false;
        }
        rec.t = t; rec.p = ray.origin + t*ray.direction;
        rec.normal = (rec.p-g.a.xyz())/g.a.w;
        rec.geometric = rec.normal;rec.front_face=dot(ray.direction,rec.normal)<0;
        rec.bary=f3(1,0,0); rec.has_uv = false; rec.uv = f2(0); rec.material = g.meta.y;
        return true;
    }
    float3 e1 = g.b.xyz()-g.a.xyz(), e2 = g.c.xyz()-g.a.xyz();
    float3 pvec = cross(ray.direction, e2);
    float det = dot(e1, pvec);
    if (abs(det) < 1e-8f) return false;
    float inv = 1.0f/det;
    float3 tvec = ray.origin-g.a.xyz();
    float b1 = dot(tvec, pvec)*inv;
    if (b1 < 0 || b1 > 1) return false;
    float3 qvec = cross(tvec, e1);
    float b2 = dot(ray.direction, qvec)*inv;
    if (b2 < 0 || b1+b2 > 1) return false;
    float t = dot(e2, qvec)*inv;
    if (t < t_min || t > t_max) return false;
    float b0 = 1-b1-b2;rec.bary=f3(b0,b1,b2);
    rec.t = t; rec.p = ray.origin+t*ray.direction; rec.material = g.meta.y;
    rec.has_uv = (g.meta.z & 1u) != 0;
    rec.uv = rec.has_uv ? b0*g.uv01.xy()+b1*g.uv01.zw()+b2*g.uv2.xy() : f2(0);
    float3 ng = normalize(cross(e1, e2)), ns = ng;
    float winding=(g.meta.z&2u) && dot(ng,g.n0.xyz()+g.n1.xyz()+g.n2.xyz())<0 ? -1.0f : 1.0f;
    rec.front_face=dot(ray.direction,ng)*winding<0;
    if (g.meta.z & 2u) {
        float3 interpolated = b0*g.n0.xyz()+b1*g.n1.xyz()+b2*g.n2.xyz();
        if (dot(interpolated, interpolated) > 1e-20f) ns = normalize(interpolated);
        if (dot(ns, ng) < 0) ns = -ns;
    }
    if (dot(ray.direction, ng) > 0) { ng = -ng; ns = -ns; }
    if (dot(ray.direction, ns) >= 0) ns = ng;
    rec.normal = ns; rec.geometric = ng;
    return true;
}
// Adapted geometry equations from HoyoToon zzz-program.hlsl vs_outline.
// ZZZATTR3 directions are decoded from the selected source UV in HoyoToon's
// tangent frame. Older PMX packages retain their generated direction fallback.
PT_FN float3 hull_vertex(float3 position, float4 direction, float width,
                          GPUParams &p,const GPUMaterial &m)
{
    float3 right=p.right.xyz(),up=p.up.xyz(),back=normalize(cross(right,up));
    float3 delta=position-p.origin.xyz();
    float3 view=f3(dot(delta,right),dot(delta,up),dot(delta,back));
    float z=m.zzz_outline_rules.w ? dot(direction.xyz(),back) : -0.0001f;
    float3 normal=f3(dot(direction.xyz(),right),dot(direction.xyz(),up),z);
    float3 unit=normalize(normal);
    float focus=length(p.lower_left.xyz()+0.5f*p.horizontal.xyz()+0.5f*p.vertical.xyz()-p.origin.xyz());
    float tan_half_fov=length(p.vertical.xyz())/(2*focus);
    float fov=m.zzz_outline_rules.z ? 1.0f : 2.414f*tan_half_fov;
    float offset=mix(0.001f,0.01f,clamp(1+view.z*fov,0.0f,1.0f))*0.01f;
    // Unity's view-space -Z maps to -back. Keep the tiny unnormalised Z term.
    return position+delta*offset+width*direction.w*(right*unit.x+up*unit.y+back*z);
}
PT_FN bool hull_hit(const GPUPrimitive &source, const GPUMaterial &m,
                     Ray ray,float t_min,float t_max,Hit &hit,
                     GPUParams &p)
{
    if (source.meta.x!=1 || m.flags.x!=4 || m.mmd.w>0 || m.edge.x<=0 || m.edge.y<=0) return false;
    GPUPrimitive original=source;Hit original_hit;
    // Open/alpha-cutout MMD garments expose their back faces. They are not
    // a closed hull: do not paint their unexpanded interior as an outline.
    // Only newly exposed expanded back-face pixels are eligible on these rays.
    if (primitive_hit(original,ray,t_min,t_max,original_hit)) return false;
    GPUPrimitive hull=source;
    hull.a.set_xyz(hull_vertex(source.a.xyz(),source.outline0,m.edge.y,p,m));
    hull.b.set_xyz(hull_vertex(source.b.xyz(),source.outline1,m.edge.y,p,m));
    hull.c.set_xyz(hull_vertex(source.c.xyz(),source.outline2,m.edge.y,p,m));
    float3 face=cross(hull.b.xyz()-hull.a.xyz(),hull.c.xyz()-hull.a.xyz());
    // Preserve the original winding-to-outward-normal convention after expansion.
    float sign=dot(cross(source.b.xyz()-source.a.xyz(),source.c.xyz()-source.a.xyz()),
                   source.n0.xyz()+source.n1.xyz()+source.n2.xyz())>=0 ? 1.0f : -1.0f;
    if (dot(face,ray.direction)*sign<=0) return false; // HoyoToon Cull Front.
    return primitive_hit(hull,ray,t_min,t_max,hit);
}
// layer=0: surfaces; 1: overlays; 2: shadow casters; 3: hull; 4: ZZZ hair; 5: eye/brow.
// Shells are handled separately so coincident hair geometry never becomes opaque.
RT_QUERY_FN bool software_world_hit(const Scene &s, Ray ray, float t_min, float t_max,
                      Hit &rec, uint layer = 0)
{
    // Host median-split BVH has depth <= 32 for representable primitive counts.
    uint stack[64], top = 0;
    stack[top++] = 0;
    bool found = false;
    // An inverted hull is intersectable geometry here, including later bounces.
    // Its global shape follows the fixed render camera; no final-image outline overlay.
    bool path_hulls=(s.camera->features.x&2u) && !(s.camera->features.x&4u) && (layer==0 || layer==2);
    while (top) {
        uint i = stack[--top];
        const GPUNode &n = s.nodes[i];
        float entry;
        float padding=layer==3 || path_hulls ? s.camera->filter.w : 0.0f;
        if (!box_hit(n, ray, t_min, t_max, entry,padding)) continue;
        if (n.link.w) {
            const GPUPrimitive &primitive = s.primitives[n.link.z];
            const GPUMaterial &material = s.materials[primitive.meta.y];
            bool overlay = material.flags.x == 4 && material.mmd.w > 0;
            bool baked_hull=primitive.meta.x==2;
            if(baked_hull && (!(s.camera->features.x&2u) || (layer!=0 && layer!=2))) continue;
            if ((layer == 1) != overlay) continue;
            // The shadow ray targets the light's centre. The emitting light
            // surface must not be counted as an occluder of its own light.
            if (layer == 4 && (material.zzz_misc.w<=0 || uint(material.zzz_misc.y)!=4)) continue;
            if (layer == 4 && !material.mmd_flags.z) continue;
            if (layer == 2 && material.flags.x == 3 && !material.flags.w) continue;
            if (layer == 2 && material.flags.x == 4 && !material.mmd_flags.z) continue;
            if (layer==5 && (material.zzz_face_detail.w<=0 || material.zzz_render.w<1 || material.zzz_render.w>2)) continue;
            GPUPrimitive local=primitive;Hit candidate;
            bool intersects=layer==3 ? hull_hit(primitive,material,ray,t_min,t_max,candidate,*s.camera)
                                    : primitive_hit(local,ray,t_min,t_max,candidate);
            if(intersects && baked_hull) {
                GPUPrimitive original=s.primitives[primitive.meta.w>>8];Hit original_hit;
                float sign=(primitive.meta.z&4u) ? -1.0f : 1.0f;
                if(dot(cross(primitive.b.xyz()-primitive.a.xyz(),primitive.c.xyz()-primitive.a.xyz()),ray.direction)*sign<=0 ||
                   primitive_hit(original,ray,t_min,t_max,original_hit)) intersects=false;
                else candidate.outline=true;
            }
            if (intersects && !baked_hull && layer!=3 && material.zzz_face_detail.w>0 &&
                ((material.zzz_render.z==1 && candidate.front_face) || (material.zzz_render.z==2 && !candidate.front_face))) intersects=false;
            if (path_hulls) {
                Hit ink;
                if (hull_hit(primitive,material,ray,t_min,intersects ? candidate.t : t_max,ink,*s.camera)) {
                    candidate=ink;candidate.outline=true;intersects=true;
                }
            }
            if (intersects) {
                // A rejected backface must never overwrite the nearest accepted hit.
                candidate.primitive=n.link.z;rec=candidate;
                found = true; t_max = rec.t;
            }
        } else {
            float tl, tr;
            bool left = box_hit(s.nodes[n.link.x], ray, t_min, t_max, tl,padding);
            bool right = box_hit(s.nodes[n.link.y], ray, t_min, t_max, tr,padding);
            if (left && right) {
                stack[top++] = tl <= tr ? n.link.y : n.link.x;
                stack[top++] = tl <= tr ? n.link.x : n.link.y;
            } else if (left) stack[top++] = n.link.x;
            else if (right) stack[top++] = n.link.y;
        }
    }
    return found;
}
RT_QUERY_FN bool optix_world_hit(const Scene &s, Ray ray, float t_min, float t_max,
                      Hit &rec, uint layer)
;
PT_NOINLINE bool world_hit(const Scene &s, Ray ray, float t_min, float t_max,
                      Hit &rec, uint layer = 0)
{
#if defined(__CUDA_ARCH__)
    return optix_world_hit(s, ray, t_min, t_max, rec, layer);
#else
    return software_world_hit(s, ray, t_min, t_max, rec, layer);
#endif
}

PT_FN float srgb_decode(float x)
{
    return x <= 0.04045f ? x/12.92f : pow((x+0.055f)/1.055f, 2.4f);
}
PT_FN float3 albedo_at(const Scene &s, const Hit &h)
{
    const GPUMaterial &m = s.materials[h.material];
    if (!m.image.w) return m.color.xyz();
    if (!h.has_uv) return f3(1, 0, 1);
    float2 uv = h.uv*m.uv_transform.xy()+m.uv_transform.zw();
    if (!all(isfinite(uv))) return f3(1, 0, 1);
    if (m.flags.y) uv = clamp(uv, 0.0f, 1.0f);
    else uv -= floor(uv);
    uint x = uint(floor(uv.x*m.image.y));
    uint y = uint(floor((1-uv.y)*m.image.z));
    if (m.flags.y) { x = min(x, m.image.y-1); y = min(y, m.image.z-1); }
    else { x %= m.image.y; y %= m.image.z; }
    uint i = m.image.x+4*(y*m.image.y+x);
    float3 c = f3(s.pixels[i], s.pixels[i+1], s.pixels[i+2])/255.0f;
    if (m.flags.z) c = f3(srgb_decode(c.x), srgb_decode(c.y), srgb_decode(c.z));
    return m.color.xyz()*c;
}

// The MMD branch uses bilinear filtering. Coordinates are bottom-up UV; file
// pixels are top-down. Control/alpha values are never gamma converted.
PT_FN float4 mmd_texel(const Scene &s, uint4 image, int2 xy, bool srgb)
{
    xy = clamp(xy, i2(0), i2(image.yz())-1);
    uint offset = image.x+4*(uint(xy.y)*image.y+uint(xy.x));
    float4 value = f4(s.pixels[offset],s.pixels[offset+1],
                         s.pixels[offset+2],s.pixels[offset+3])/255.0f;
    if (srgb) value.set_xyz(f3(srgb_decode(value.x),srgb_decode(value.y),srgb_decode(value.z)));
    return value;
}
PT_FN float4 mmd_texture(const Scene &s, uint4 image, float2 uv,
                          bool srgb, bool repeat)
{
    if (!image.w) return f4(1);
    if (!all(isfinite(uv))) return f4(1,0,1,1);
    uv = repeat ? uv-floor(uv) : clamp(uv,0.0f,1.0f);
    float2 point = f2(uv.x,1-uv.y)*f2(image.yz())-0.5f;
    int2 pixel = i2(floor(point));
    float2 f = fract(point);
    int2 a=pixel, b=pixel+i2(1,0), c=pixel+i2(0,1), d=pixel+1;
    if (repeat) {
        int2 size=i2(image.yz());
        a=(a%size+size)%size; b=(b%size+size)%size;
        c=(c%size+size)%size; d=(d%size+size)%size;
    }
    return mix(mix(mmd_texel(s,image,a,srgb),mmd_texel(s,image,b,srgb),f.x),
               mix(mmd_texel(s,image,c,srgb),mmd_texel(s,image,d,srgb),f.x),f.y);
}
PT_FN float2 zzz_selected_uv(const Scene &s,const Hit &h,uint selector)
{
    const GPUPrimitive &g=s.primitives[h.primitive];
    if(selector==1 && (g.meta.w&2u)) return h.bary.x*g.face0.xy()+h.bary.y*g.face1.xy()+h.bary.z*g.face2.xy();
    if(selector>=2 && (g.meta.w&4u)) {
        float4 uv=h.bary.x*g.extra_uv0+h.bary.y*g.extra_uv1+h.bary.z*g.extra_uv2;
        return selector==2 ? uv.xy() : uv.zw();
    }
    return h.uv;
}
PT_FN float2 zzz_light_uv(const Scene &s,const Hit &h)
{
    const GPUMaterial &m=s.materials[h.material];
    return m.zzz_alpha.w>0 && !h.front_face ? zzz_selected_uv(s,h,m.zzz_uv_rules.x) : h.uv;
}
PT_FN float2 surface_uv(const Scene &s, const Hit &h)
{
    const GPUMaterial &m=s.materials[h.material];
    float2 uv=h.uv;
    if (m.zzz_face_detail.w>0 && m.zzz_alpha.w>0 && !h.front_face) {
        uv=zzz_selected_uv(s,h,m.zzz_uv_rules.x);
    }
    if ((s.camera->rendering.w&1u) && m.zzz_misc.w>0) {
        // ps_model selects raw UVs; it does not multiply these maps by _Tex_ST.
        if(m.zzz_uv_rules.y && zzz_selected_uv(s,h,1).x>1)
            uv=zzz_selected_uv(s,h,m.zzz_uv_rules.x);
        return uv;
    }
    return uv*m.uv_transform.xy()+m.uv_transform.zw();
}
PT_FN float4 mmd_base(const Scene &s, const Hit &h)
{
    const GPUMaterial &m=s.materials[h.material];
    float2 uv=surface_uv(s,h);
    float4 value=mmd_texture(s,m.image,uv,m.flags.z!=0,m.flags.y==0);
    value.set_xyz(value.xyz() * (m.color.xyz()));
    if (m.zzz_face_detail.w>0) {
        if (m.zzz_render.x==0 || (s.camera->features.w&2048u)) value.w=m.mmd.x;
        else {
            if (m.zzz_render.y==1 && m.roughness_image.w)
                value.w=mmd_texture(s,m.roughness_image,uv,false,m.flags.y==0).r;
            value.w=clamp(value.w*m.mmd.x,0.0f,1.0f);
            if (m.zzz_render.x==1) value.w=value.w>=m.zzz_alpha.x ? 1.0f : 0.0f;
        }
    } else {
        value.w*=m.mmd.x;
        if (m.zzz_misc.w>0 && m.zzz_misc.z==0) value.w=m.mmd.x;
    }
    return value;
}
PT_FN void apply_normal_map(const Scene &s, Hit &h, GPUParams &p)
{
    const GPUMaterial &m=s.materials[h.material];
    const GPUPrimitive &g=s.primitives[h.primitive];
    if (h.outline) return;
    if ((p.rendering.w&1) && m.zzz_misc.w>0 && m.zzz_source.z<=0) return;
    if (!p.features.y || !m.normal_image.w || !h.has_uv || g.meta.x!=1 || m.surface.z<=0) return;
    float2 duv1=(g.uv01.zw()-g.uv01.xy())*m.uv_transform.xy();
    float2 duv2=(g.uv2.xy()-g.uv01.xy())*m.uv_transform.xy();
    float determinant=duv1.x*duv2.y-duv1.y*duv2.x;
    if (abs(determinant)<1e-10f) return; // A degenerate UV face has no usable tangent frame.
    float3 e1=g.b.xyz()-g.a.xyz(),e2=g.c.xyz()-g.a.xyz();
    float3 tangent=(e1*duv2.y-e2*duv1.y)/determinant;
    float3 raw_bitangent=(e2*duv1.x-e1*duv2.x)/determinant;
    tangent-=h.normal*dot(h.normal,tangent);
    if (dot(tangent,tangent)<1e-15f) return;
    tangent=normalize(tangent);
    float handedness=dot(cross(h.normal,tangent),raw_bitangent)<0 ? -1.0f : 1.0f;
    float3 bitangent=handedness*cross(h.normal,tangent);
    // Normal maps are linear directions, never sRGB colors. Mirrored UVs
    // retain their handedness; a DirectX map can explicitly flip its green channel.
    float2 normal_uv=(p.rendering.w&1u) && m.zzz_misc.w>0 ? zzz_light_uv(s,h) : surface_uv(s,h);
    float3 local=2*mmd_texture(s,m.normal_image,normal_uv,false,m.flags.y==0).xyz()-1;
    if (m.zzz_misc.w>0) {
        if (uint(m.zzz_misc.y)==1 || uint(m.zzz_misc.y)==2) return;
        // ZZZ N.B is a light-control channel, never the normal's Z component.
        local.set_xy(local.xy() * (m.zzz_params.w));
        local.z=sqrt(max(0.0f,1.0f-min(1.0f,dot(local.xy(),local.xy()))));
        if (g.meta.w) {
            float3 source_normal=normalize(h.bary.x*g.n0.xyz()+h.bary.y*g.n1.xyz()+h.bary.z*g.n2.xyz());
            float4 source_tangent=h.bary.x*g.tangent0+h.bary.y*g.tangent1+h.bary.z*g.tangent2;
            tangent=source_tangent.xyz()-source_normal*dot(source_normal,source_tangent.xyz());
            if (dot(tangent,tangent)<1e-12f) return;
            tangent=normalize(tangent);
            bitangent=(source_tangent.w<0 ? -1.0f : 1.0f)*cross(source_normal,tangent);
            if (dot(source_normal,h.normal)<0) local.z=-local.z;
            float3 mapped=normalize(local.x*tangent+local.y*bitangent+local.z*source_normal);
            if (dot(mapped,h.geometric)>0.05f) h.normal=mapped;
            return;
        }
        float3 mapped=normalize(local.x*tangent+local.y*bitangent+local.z*h.normal);
        if (dot(mapped,h.geometric)>0.05f) h.normal=mapped;
        return;
    }
    local.set_xy(local.xy() * (m.surface.z));
    if (m.surface_flags.z) local.y=-local.y;
    if (dot(local,local)<1e-12f) return;
    float3 mapped=normalize(local.x*tangent+local.y*bitangent+local.z*h.normal);
    // Maps perturb shading only. Geometry, offsets and outline widths remain unchanged.
    if (dot(mapped,h.geometric)>0.05f) h.normal=mapped;
}
PT_FN float3 sky_color(float3 direction)
{
    float t=0.5f*(normalize(direction).y+1);
    return mix(f3(1),f3(0.5f,0.7f,1),t);
}
PT_FN float3 surface_reflection(const Scene &s, const Hit &h,
                                GPUParams &p, float3 base, float3 toon)
{
    const GPUMaterial &m=s.materials[h.material];
    if (!p.features.z || m.surface.w<=0) return toon;
    float2 uv=surface_uv(s,h);
    float metallic=m.surface.x,roughness=m.surface.y;
    if (m.metallic_image.w) metallic*=mmd_texture(s,m.metallic_image,uv,false,m.flags.y==0)[m.surface_flags.x];
    if (m.roughness_image.w) roughness*=mmd_texture(s,m.roughness_image,uv,false,m.flags.y==0)[m.surface_flags.y];
    metallic=clamp(metallic,0.0f,1.0f);roughness=clamp(roughness,0.08f,1.0f);
    float3 n=h.normal,v=normalize(p.origin.xyz()-h.p),delta=LIGHT_CENTER-h.p;
    float3 l=normalize(delta),sum=v+l;
    float3 half_vector=dot(sum,sum)>1e-12f ? normalize(sum) : n;
    float nv=max(dot(n,v),0.001f),nl=max(dot(n,l),0.0f);
    float nh=max(dot(n,half_vector),0.0f),vh=max(dot(v,half_vector),0.0f);
    float3 f0=mix(f3(0.04f),base,metallic);
    float3 fresnel=f0+(1-f0)*pow(1-vh,5.0f);
    // Cook-Torrance GGX: roughness controls microfacet spread, not color blur.
    float alpha=roughness*roughness,a2=alpha*alpha;
    float denominator=nh*nh*(a2-1)+1;
    float distribution=a2/(PI*denominator*denominator);
    float gv=nl*sqrt(nv*nv*(1-a2)+a2);
    float gl=nv*sqrt(nl*nl*(1-a2)+a2);
    float visibility=0.5f/max(gv+gl,1e-6f);
    float3 specular=distribution*visibility*fresnel*nl*LIGHT_EMISSION*
                    (PI*LIGHT_RADIUS*LIGHT_RADIUS/max(dot(delta,delta),0.001f));
    // A deliberately inexpensive environment approximation for the Toon branch:
    // it samples the existing sky and blends toward its hemisphere average as
    // roughness rises. It does not trace reflections of other scene objects.
    float3 reflected=reflect(-v,n);
    float3 environment=mix(sky_color(reflected),f3(0.75f,0.85f,1),roughness*roughness);
    float3 environment_fresnel=f0+(max(f3(1-roughness),f0)-f0)*pow(1-nv,5.0f);
    float3 pbr=(1-metallic)*toon+environment*environment_fresnel+specular;
    return mix(toon,pbr,m.surface.w);
}
PT_FN float3 offset_mmd_ray(const Hit &hit, float3 direction)
{
    // Start outside the surface using its geometric normal, not the smoothed
    // shading normal. Otherwise adjacent triangles can shadow their own skin.
    float sign = dot(hit.geometric,direction)>=0 ? 1.0f : -1.0f;
    return hit.p+sign*0.001f*hit.geometric;
}
PT_FN float mmd_shadow(const Scene &s, const Hit &hit,
                        float3 direction, float distance, int target_light=-1)
{
    float3 point=offset_mmd_ray(hit,direction);
    float visibility=1, minimum=0.0001f;
    Hit blocker;
    for (uint layer=0;layer<64;++layer) {
        if (!world_hit(s,Ray{point,direction},minimum,distance-0.0001f,blocker,2)) break;
        const GPUMaterial &m=s.materials[blocker.material];
        if (m.flags.x==3 && target_light>=0 && m.flags.w==uint(target_light)+1) {
            minimum=blocker.t+0.00001f;continue; // The sampled emitter is not its own blocker.
        }
        float alpha=m.flags.x==4 ? mmd_base(s,blocker).w : 1;
        visibility*=1-clamp(alpha,0.0f,1.0f);
        if (visibility<0.001f) return 0;
        minimum=blocker.t+0.00001f;
    }
    return visibility;
}
// HoyoToon contributors, GPL-3.0: zzz-common.hlsl.
// Backend adaptation of normalize_color, shadow_body and specular. Per-region
// controls are supplied by the original material JSON, not procedural presets.
PT_FN float3 zzz_normalize_color(float3 color, float4 tmp)
{
    float2 magic = f2(0.562750012, 0.437249988);
    float tmp2;
    color = 0.00006f + color;
    tmp2 = tmp.x + tmp.y;
    tmp2 = tmp2 + tmp.z;
    tmp2 = 0.333330005 * tmp2;
    float3 color_div = saturate(color / max(tmp2,0.00001f));
    color = color * magic.yyy();
    color = color_div * magic.xxx() + color;
    return color;
}
PT_FN float4 zzz_shadow_body(float3 normal, float3 light, float4 tex_data, float id, float selfshadow, const GPUMaterial &m)
{
    float3 light_direction = light;

    float4 shadow_thresholds,temp0,temp1,temp2,temp3,temp4;
    
    float ao_tex = tex_data.z;
    ao_tex = ao_tex * (selfshadow);
    ao_tex = ao_tex * 2 + -1;

    uint region=id<.2f ? 4u : id<.4f ? 3u : id<.6f ? 2u : id<.8f ? 1u : 0u;
    float albedo_smoothness = max(0.00000999999975f, m.zzz_misc.x);
    float inverse_smoothness = (1.0f/albedo_smoothness);
    float ndotl = dot(normal, light_direction);
    ndotl = ndotl * saturate(selfshadow);

    float shad = ao_tex * 2 + ndotl;
    float albedo_step = -albedo_smoothness * 3 + 2;
    albedo_step = 3 / albedo_step;
    shadow_thresholds.set_yz(albedo_smoothness * f2(0.5,1.5) + f2(-0.333299994,0.333299994));
    shadow_thresholds.x = -1;
    shadow_thresholds.set_xyz(-shadow_thresholds.xyz() + shad); 
    temp0.set_xyw(shadow_thresholds.xyz() * albedo_step); 
    shadow_thresholds.set_xyz(-shadow_thresholds.xyz() * albedo_step + f3(1,1,1));
    temp1.set_xyz(f3(0.333299994,-0.333299994,-0.333299994) + shad);
    temp1.set_xyz(temp1.xyz() * inverse_smoothness + f3(0.5,0.5,-0.5));
    temp2.set_xyz(f3(1,1,1) + -temp1.xyz());
    temp3.set_xy(min(temp2.yx(), temp0.yx()));
    temp0.set_xz(min(temp1.xz(), shadow_thresholds.yz()));
    temp3.z = shadow_thresholds.x;
    temp3.w = temp0.x;
    shadow_thresholds.set_xyz(saturate(temp3.zyw()));

    temp3.y = saturate(min(temp2.z, temp1.y));
    temp3.x = saturate(temp3.x);
    temp0.set_zw(saturate(temp0.zw()));
    temp1.set_xyzw(1.0f * f4(-2,2,2,-2) + f4(1,0,-1,2));
    temp1.y = saturate(min(temp1.y, temp1.w));
    temp1.set_xz(saturate(temp1.xz()));
    
        
    temp0.set_xy(temp1.xy());
    inverse_smoothness = 1 + -shadow_thresholds.x;
    inverse_smoothness = inverse_smoothness + -shadow_thresholds.y;
    inverse_smoothness = inverse_smoothness + -shadow_thresholds.z;
    inverse_smoothness = temp0.x * inverse_smoothness + shadow_thresholds.z;
    albedo_smoothness = temp1.y + temp1.z;
    shadow_thresholds.set_zw(temp3.xy() * albedo_smoothness);
    albedo_smoothness = temp0.z + temp0.w;
    albedo_smoothness = albedo_smoothness * temp0.y + shadow_thresholds.w;
    albedo_step = temp1.z * temp0.z;
    shadow_thresholds.x =  shadow_thresholds.x;
    shadow_thresholds.x = shadow_thresholds.y + shadow_thresholds.x;
    
    float3 shallow_color = m.zzz_shallow[0].xyz();
    shallow_color = id < 0.8f ? m.zzz_shallow[1].xyz() : shallow_color;
    shallow_color = id < 0.6f ? m.zzz_shallow[2].xyz() : shallow_color;
    shallow_color = id < 0.4f ? m.zzz_shallow[3].xyz() : shallow_color;
    shallow_color = id < 0.2f ? m.zzz_shallow[4].xyz() : shallow_color;

    float3 shadow_color = m.zzz_shadow[0].xyz();
    shadow_color = id < 0.8f ? m.zzz_shadow[1].xyz() : shadow_color;
    shadow_color = id < 0.6f ? m.zzz_shadow[2].xyz() : shadow_color;
    shadow_color = id < 0.4f ? m.zzz_shadow[3].xyz() : shadow_color;
    shadow_color = id < 0.2f ? m.zzz_shadow[4].xyz() : shadow_color;
    

    shallow_color = zzz_normalize_color(shallow_color, temp0);
    shadow_color = zzz_normalize_color(shadow_color, temp1);


    float3 post_shallow;
    post_shallow = m.zzz_post[0].xyz() * shallow_color;
    shallow_color = m.zzz_post[1].xyz() * shallow_color;

    float3 post_shadow;
    post_shadow = m.zzz_post[2].xyz() * shadow_color;
    shadow_color = m.zzz_post[3].xyz() * shadow_color;
    temp4.set_xyz(1.17549435e-38 + 1);
    shadow_thresholds.y = max(temp4.x, temp4.y);
    shadow_thresholds.y = max(shadow_thresholds.y, temp4.z);
    shadow_thresholds.y = (1.0f/shadow_thresholds.y);
    
    float3 post_fss_tint;
    post_fss_tint = m.zzz_post[4].xyz() * albedo_step;
    post_fss_tint = m.zzz_post[5].xyz() * albedo_smoothness + post_fss_tint;
    post_fss_tint = temp0.www() * temp1.zzz() + post_fss_tint;
    post_shadow = post_shadow * shadow_thresholds.xxx();

    float3 final_color;
    final_color = shallow_color * inverse_smoothness + post_shadow;
    final_color = post_shallow * shadow_thresholds.zzz() + final_color;
    final_color = final_color;
    final_color = post_fss_tint + final_color;
    

    return f4(saturate(final_color), shad);
}
// Authored hair extension, not an original HoyoToon formula. Stretch the
// highlight's angular coordinates in the original strand tangent frame.
// Product of the two stretches is one. A=0 returns the original half-vector;
// |A|=1 gives a 4:1 / 1:4 coordinate stretch; negative A swaps the long axis.
// This modifies the material response at every path hit, never the final image.
PT_FN float3 designed_highlight_half(float3 normal,float3 view,float3 light,float3 tangent,float anisotropy)
{
    float3 sum=view+light;
    float3 h=dot(sum,sum)>1e-12f ? normalize(sum) : normal;
    if(abs(anisotropy)<1e-6f) return h;
    float3 t=tangent-normal*dot(normal,tangent);
    if(dot(t,t)<1e-12f) return h;
    t=normalize(t);float3 b=cross(normal,t);
    float stretch=exp2(2*abs(anisotropy));
    if(anisotropy<0) stretch=1/stretch;
    return normalize(t*(dot(h,t)/stretch)+b*(dot(h,b)*stretch)+normal*dot(h,normal));
}
PT_NOINLINE float3 zzz_specular(float3 normal, float3 view, float3 light, float4 other_data, float4 other_data2, float3 color, float shad, float3 pos, const GPUMaterial &m,
                                            float3 strand_tangent=f3(1,0,0),float anisotropy=0)
{   
    float4 r0=f4(0), r1=f4(0), r2=f4(0), r3=f4(0), r4=f4(0), r5=f4(0), r6=f4(0), r7=f4(0), r8=f4(0), r9=f4(0), r10=f4(0), r11=f4(0), r12=f4(0), r13=f4(0), r14=f4(0), r15=f4(0);
    r8.w = dot(light.xyz(), normal.xyz());
    r0.x = m.zzz_params.x * other_data.y;
    r3.x = other_data.z;
    r0.y = other_data2.y;
    r7.set_xyz(color);
    bool4 check = other_data.xxxx() < f4(0.2f, 0.4f, 0.6f, 0.8f);
    float3 some_color = color;
    r0.w = dot(some_color, f3(0.289999992,0.600000024,0.109999999));
    r0.w = r0.w * 0.287499994 + 1.4375;
    r9.x = dot(normal.xyz(), light.xyz());
    r9.y = r9.x + -r8.w;
    r9.y = saturate(-r9.y * 3 + 1);
    r9.z = r9.y + r9.y;
    r9.y = sqrt(r9.y);
    r9.y = r9.z * r9.y;
    r9.y = min(1.0f, r9.y);
    r9.z = r8.w * 0.5 + 0.5;
    r9.w = saturate(r8.w);
    r9.y = r9.z * r9.y + -r9.w;
    r9.y = r9.y * 0.5 + r9.w;
    r9.x = saturate(r9.x);
    r9.z = max(color.y, color.z);
    r9.z = max(r9.z, color.x);
    r10.x = (1 < r9.z);
    r10.set_yzw(some_color / max(r9.zzz(),f3(0.00001f)));
    r10.set_xyz(select(some_color,r10.yzw(),b3(r10.x!=0)));
    r9.z = 1 + -r0.w;
    float power = r9.y * r9.z + r0.w; 
    
    r10.set_xyz(pow(r10.xyz(), power));
    
    
    r11.set_xyz(r10.xyz() + -some_color);

    some_color = r11.xyz() * f3(0.5,0.5,0.5) + some_color;
    r10.set_xyz(r10.xyz() + -some_color);
    some_color = r9.xxx() * r10.xyz() + some_color;
    r0.w = -r0.x * 0.959999979 + 0.959999979;
    r9.set_xyz(some_color * r0.www());
    r10.set_xyz(f3(-0.0399999991,-0.0399999991,-0.0399999991) + some_color);
    r10.set_xyz(r0.xxx() * r10.xyz() + f3(0.0399999991,0.0399999991,0.0399999991));
    r10.w = -r0.y * m.zzz_params.y + 1;
    r10.w = r10.w * r10.w;
    r11.x = r10.w * 4 + 2;
    r11.y = r10.w * r10.w;
    r11.z = r10.w * r10.w + -1;
    r11.w = check.w ? m.zzz_region[1].x : m.zzz_region[0].x;
    r11.w = check.z ? m.zzz_region[2].x : r11.w;
    r11.w = check.y ? m.zzz_region[3].x : r11.w;
    r11.w = check.x ? m.zzz_region[4].x : r11.w;
    r12.set_xyz(select(m.zzz_specular[0].xyz(),m.zzz_specular[1].xyz(),check.www()));
    r12.set_xyz(select(r12.xyz(),m.zzz_specular[2].xyz(),check.zzz()));
    r12.set_xyz(select(r12.xyz(),m.zzz_specular[3].xyz(),check.yyy()));
    r12.set_xyz(select(r12.xyz(),m.zzz_specular[4].xyz(),check.xxx()));
    r12.set_xyz(r12.xyz() * 0.5f);
    r12.w = (0.5 < r11.w);
    if (r12.w != 0) {
        r4.w = saturate(shad * 1.5 + 0.5);
        r3.z = check.w ? m.zzz_region[1].y : m.zzz_region[0].y;
        r3.z = check.z ? m.zzz_region[2].y : r3.z;
        r3.z = check.y ? m.zzz_region[3].y : r3.z;
        r3.z = check.x ? m.zzz_region[4].y : r3.z;

        r14.set_xyz(designed_highlight_half(normal,view,light,strand_tangent,anisotropy));
        r12.w = (0 < m.zzz_head.w);
        r15.set_xyz(-m.zzz_head.xyz() + pos.xyz());
        r14.w = dot(r15.xyz(), r15.xyz());
        
        r14.w = sqrt(r14.w);
        r14.w = -m.zzz_head.w + r14.w;
        r14.w = saturate(20 * r14.w);
        r14.w = 1 + -r14.w;
        r15.set_xyz((dot(r15.xyz(),r15.xyz())>1e-12f ? normalize(r15.xyz()) : normal) + -normal.xyz());
        r15.set_xyz(r14.www() * r15.xyz() + normal.xyz());
        r14.w = dot(light, r15.xyz());
        r14.w = saturate(r14.w * 0.5 + 0.5);
        r15.w = sqrt(r14.w);
        r15.set_xyzw(r12.w!=0 ? r15.xyzw() : f4(normal, r4.w));
        r4.w = dot(r15.xyz(), r14.xyz());
        r4.w = saturate(r4.w * 0.5 + 0.5);
        r4.w = -r4.w * r15.w + 1;
        r4.w = -r4.w + other_data.z;
        r3.x = saturate(r4.w / r3.z);
    }
    r3.x = (m.zzz_params.z * 10) * r3.x;
    r12.set_xyz(r3.xxx() * r12.xyz());
    r12.set_xyz(r12.xyz() * r10.xyz());
    r3.x = (r11.w < 0.5);
    r5.set_xyz(designed_highlight_half(normal,view,light,strand_tangent,anisotropy));
    r3.z = check.w ? m.zzz_region[1].z : m.zzz_region[0].z;
    r3.z = check.z ? m.zzz_region[2].z : r3.z;
    r3.z = check.y ? m.zzz_region[3].z : r3.z;
    r3.z = check.x ? m.zzz_region[4].z : r3.z;
    r3.w = r3.z * r8.w;
    r3.w = saturate(r3.w * 0.75 + 0.25);
    r4.w = dot(normal, r5.xyz());
    r4.w = r4.w * r3.z;
    r4.w = saturate(r4.w * 0.75 + 0.25);
    r5.x = dot(light, r5.xyz());
    r3.z = r5.x * r3.z;
    r3.z = saturate(r3.z * 0.75 + 0.25);
    r4.w = r4.w * r4.w;
    r4.w = r4.w * r11.z + 1.00001001;
    r3.z = r3.z * r3.z;
    r4.w = r4.w * r4.w;
    r3.z = max(0.100000001, r3.z);
    r4.w = r4.w * r3.z;
    r4.w = r4.w * r11.x;
    r4.w = r11.y / r4.w;
    r0.y = saturate(-r0.y * m.zzz_params.y + r4.w);
    r0.y = r0.y * r3.w;
    r4.w = max(9.99999975e-06, r10.w);
    r0.y = r0.y / r4.w;
    r4.w = check.w ? m.zzz_region[1].w : m.zzz_region[0].w;
    r4.w = check.z ? m.zzz_region[2].w : r4.w;
    r4.w = check.y ? m.zzz_region[3].w : r4.w;
    r4.w = check.x ? m.zzz_region[4].w : r4.w;
    r5.x = check.w ? 1.0f : 1.0f;
    r5.x = check.z ? 1.0f : r5.x;
    r5.x = check.y ? 1.0f : r5.x;
    r5.x = check.x ? 1.0f : r5.x;
    r4.w = r5.x * r4.w;
    r0.y = r4.w * r0.y;
    r0.y = saturate(10 * r0.y);
    r0.y = 100 * r0.y;
    r3.z = 0.166663334 / r3.z;
    r3.z = min(1.0f, r3.z);
    r3.z = r3.z * r3.w;
    r3.z = 100 * r3.z;
    r0.y = r3.x ? r0.y : r3.z;
    r3.set_xzw(r0.yyy() * r12.xyz());
    r5.set_xyz(r3.xzw() * r7.xyz());
    return r5.xyz();
}
// Adapted from HoyoToon zzz-common.hlsl shadow_face (GPL-3.0).
PT_FN float3 zzz_shadow_face(float id, float shad_area, float face_value, const GPUMaterial &m)
{



	float4 shadow_thresholds=0,temp0=0,temp1=0,temp2=0,temp3=0,temp4=0;


    float albedo_smoothness = (face_value < 0.5) ? m.zzz_misc.x : 0.025;
    albedo_smoothness =  max(0.00000999999975f, m.zzz_misc.x);
    float inverse_smoothness = (1.0f/albedo_smoothness);
    float ndotl = shad_area;
    shadow_thresholds.x = 1;
    float shadow_area = ndotl;
    float albedo_step = -albedo_smoothness * 3 + 2;
    albedo_step = 3 / albedo_step;
    shadow_thresholds.set_yz(albedo_smoothness * f2(0.5,1.5) + f2(-0.333299994,0.333299994));
    shadow_thresholds.x = -1;
    shadow_thresholds.set_xyz(-shadow_thresholds.xyz() + shadow_area);
    temp0.set_xyw(shadow_thresholds.xyz() * albedo_step);
    shadow_thresholds.set_xyz(-shadow_thresholds.xyz() * albedo_step + f3(1,1,1));
    temp1.set_xyz(f3(0.333299994,-0.333299994,-0.333299994) + shadow_area);
    temp1.set_xyz(temp1.xyz() * inverse_smoothness + f3(0.5,0.5,-0.5));
    temp2.set_xyz(f3(1,1,1) + -temp1.xyz());
    temp3.set_xy(min(temp2.yx(), temp0.yx()));
    temp0.set_xz(min(temp1.xz(), shadow_thresholds.yz()));
    temp3.z = shadow_thresholds.x;
    temp3.w = temp0.x;
    shadow_thresholds.set_xyz(saturate(temp3.zyw()));
    temp3.y = saturate(min(temp2.z, temp1.y));
    temp3.x = saturate(temp3.x);
    temp0.set_zw(saturate(temp0.zw()));
    temp1.set_xyzw(1.0f * f4(-2,2,2,-2) + f4(1,0,-1,2));
    temp1.y = saturate(min(temp1.y, temp1.w));
    temp1.set_xz(saturate(temp1.xz()));


    temp0.set_xy(temp1.xy());
    inverse_smoothness = 1 + -shadow_thresholds.x;
    inverse_smoothness = inverse_smoothness + -shadow_thresholds.y;
    inverse_smoothness = inverse_smoothness + -shadow_thresholds.z;
    inverse_smoothness = temp0.x * inverse_smoothness + shadow_thresholds.z;
    albedo_smoothness = temp1.y + temp1.z;
    shadow_thresholds.set_zw(temp3.xy() * albedo_smoothness);
    albedo_smoothness = temp0.z + temp0.w;
    albedo_smoothness = albedo_smoothness * temp0.y + shadow_thresholds.w;
    albedo_step = temp1.z * temp0.z;
    shadow_thresholds.x =  shadow_thresholds.x;
    shadow_thresholds.x = shadow_thresholds.y + shadow_thresholds.x;

    bool2 shad_determine = f2(id) < f2(0.6f, 0.8f);

    float3 shallow_color = shad_determine.y ? m.zzz_shallow[1].xyz() : m.zzz_shallow[0].xyz();
    shallow_color.set_xyz(shad_determine.x ? m.zzz_shallow[2].xyz() : shallow_color.xyz());
    float3 shadow_color = shad_determine.y ? m.zzz_shadow[1].xyz() : m.zzz_shadow[0].xyz();
    shadow_color.set_xyz(shad_determine.x ? m.zzz_shadow[2].xyz() : shadow_color.xyz());




    shallow_color = zzz_normalize_color(shallow_color, temp0);
    shadow_color = zzz_normalize_color(shadow_color, temp1);


    float3 post_shallow;
    post_shallow = m.zzz_post[0].xyz() * shallow_color;
    shallow_color = m.zzz_post[1].xyz() * shallow_color;

    float3 post_shadow;
    post_shadow = m.zzz_post[2].xyz() * shadow_color;
    shadow_color = m.zzz_post[3].xyz() * shadow_color;
    temp4.set_xyz(1);
    shadow_thresholds.y = max(temp4.x, temp4.y);
    shadow_thresholds.y = max(shadow_thresholds.y, temp4.z);
    shadow_thresholds.y = (1.0f/shadow_thresholds.y);

    float3 post_fss_tint;
    post_fss_tint = m.zzz_post[4].xyz() * albedo_step;
    post_fss_tint = m.zzz_post[5].xyz() * albedo_smoothness + post_fss_tint;
    post_fss_tint = temp0.www() * temp1.zzz() + post_fss_tint;
    post_shadow = post_shadow * shadow_thresholds.xxx();
    shadow_thresholds.set_xyw(post_shadow);

    float3 final_color;
    final_color = shallow_color * inverse_smoothness + shadow_thresholds.xyw();
    final_color = post_shallow * shadow_thresholds.zzz() + final_color;
    final_color = final_color;
    final_color = post_fss_tint + final_color;


    return saturate(final_color);
}


// Toon-only light override leaves the physical path tracer's emitter unchanged.
PT_FN float3 zzz_light(float3 point, GPUParams &p) {
    return p.toon_light.w>0 ? p.toon_light.xyz() : normalize(LIGHT_CENTER-point);
}
// HoyoToon vertex_face/shadow_area_face: select UV by original vertex bit,
// mirror U from head-right/light, then compare SDF.R against head-forward/light.
PT_FN float4 zzz_face_values(const Scene &s, const Hit &h,
                              float3 light, const GPUMaterial &m) {
    const GPUPrimitive &g=s.primitives[h.primitive];
    float3 weights=h.bary;
    float2 uv=h.uv;float flag=1;
    if (g.meta.w&2u) {
        uv=weights.x*(g.face0.z>0 ? g.uv01.xy() : g.face0.xy())
          +weights.y*(g.face1.z>0 ? g.uv01.zw() : g.face1.xy())
          +weights.z*(g.face2.z>0 ? g.uv2.xy() : g.face2.xy());
        flag=dot(weights,f3(g.face0.z,g.face1.z,g.face2.z));
    }
    // vs_model chooses UV0 for the legacy face; ps_model subsequently forces
    // i.test.z=1, enabling the face SDF alpha and cheek-highlight masks.
    if(m.zzz_uv_rules.w) {uv=h.uv;flag=1;}
    float right_dot=dot(m.zzz_head_right.xz(),light.xz());
    if (right_dot<=0) uv.x=1-uv.x;
    float threshold=1-(dot(m.zzz_head_forward.xz(),light.xz())*0.5f+0.5f);
    return f4(uv,flag,threshold);
}
PT_FN float zzz_face_material_id(const Scene &s, const Hit &h) {
    const GPUPrimitive &g=s.primitives[h.primitive];
    return (g.meta.w&2u) ? dot(h.bary,f3(g.face0.w,g.face1.w,g.face2.w)) : 0.9f;
}

// Adapted HoyoToon face_high: G highlight, narrow nose/cheek UV band, skin flag.
PT_FN float zzz_face_high(const Scene &s,const Hit &h,float4 fv,float3 l,float3 v,const GPUMaterial &m) {
    if (fv.z<0.5f || fv.x<=0.45f || fv.x>=0.55f) return 0;
    float3 sum=l+v;
    if (dot(sum,sum)<1e-12f) return 0;
    float ndoth=pow(clamp(dot(h.normal,normalize(sum)),0.0f,1.0f),10.0f);
    float mask=clamp(mmd_texture(s,m.zzz_face_image,fv.xy(),false,true).g-0.5f,0.0f,1.0f);
    float threshold=max(fv.w,0.75f);
    return smoothstep(threshold-0.75f,threshold+0.75f,mask)*ndoth*10*m.zzz_face_detail.x;
}
// nose_line uses raw MainTex alpha, not the opaque material's effective opacity.
PT_FN float3 zzz_nose_line(const Scene &s,const Hit &h,float3 v,uint region,const GPUMaterial &m) {
    float ndotv=pow(clamp(dot(h.normal,v),0.0f,1.0f),10.0f);
    float nose_tex=mmd_texture(s,m.image,surface_uv(s,h),false,true).a;
    float amount=clamp(smoothstep(m.zzz_face_detail.y,m.zzz_face_detail.z,ndotv)-nose_tex,0.0f,1.0f);
    float3 ink=pow(max(m.zzz_outline[region].xyz(),f3(0))*0.5f,f3(2.1f));
    return mix(f3(1),ink,amount);
}
PT_FN float3 zzz_eye_color(const Scene &s,const Hit &h,float3 base,const GPUMaterial &m) {
    // Eye/Eyebrow keep their base colour. Optional separate shadow/highlight roles
    // decode the source vertex-red nibble pair into a 16x16 colour LUT.
    if (m.zzz_render.w>=3 && m.zzz_eye_lut.w) {
        const GPUPrimitive &g=s.primitives[h.primitive];
        float red=dot(h.bary,f3(g.vertex_color0.r,g.vertex_color1.r,g.vertex_color2.r));
        uint packed=uint(clamp(red*255,0.0f,255.0f));
        int x=int(packed&15u),y=15-int(packed>>4);
        base*=mmd_texel(s,m.zzz_eye_lut,i2(x,15-y),false).xyz()*2;
    }
    return base*m.zzz_color[0].xyz();
}
// HoyoToon MatCap sampling/mask/blend; slots must be explicitly bound in JSON.
PT_FN float3 zzz_matcap_frame(const Scene &s,const Hit &h,float3 view_right,float3 view_up,uint disabled,float4 data,float4 other,float3 color) {
    const GPUMaterial &m=s.materials[h.material];
    if (m.mmd.y<=0 || m.zzz_face_detail.w<=0 || m.zzz_effects.x<=0 || (disabled&4096u)) return color;
    if (other.b<=0) return color; // Every source blend mode is identity with zero mask.
    uint id=uint(clamp(4-floor(5*data.r),0.0f,4.0f));
    // ID >= 99 enables the per-region layout in the community shader.
    float gate=m.zzz_matcap_params[0].w<99 ? m.zzz_matcap_params[0].w : float(id);
    if (gate>=50 || !m.zzz_matcap_images[id].w) return color;
    float2 uv=0.5f+0.5f*f2(dot(h.normal,view_right),dot(h.normal,view_up));
    float4 motion=m.zzz_matcap_motion[id];
    if (motion.z>0) uv=motion.w*uv+surface_uv(s,h)*m.zzz_matcap_refract[id].xy()+m.zzz_matcap_refract[id].zw();
    uv+=motion.xy()*m.zzz_effects.w;
    float4 tex=mmd_texture(s,m.zzz_matcap_images[id],uv,true,true);
    float4 params=m.zzz_matcap_params[id];
    float3 tint=tex.xyz()*m.zzz_matcap_tint[id].xyz();
    float a=clamp(tex.a*other.b,0.0f,1.0f);
    if (params.z<0.5f) return mix(color,tint*params.x,clamp(params.y*a,0.0f,1.0f));
    if (params.z<1.5f) return color+tint*clamp(params.y*a,0.0f,1.0f)*params.x;
    tint=clamp(tint*params.x+tint,0.0f,1.0f);
    tint=mix(f3(0.5f),tint,a);
    return select(2*color*tint,1-2*(1-color)*(1-tint),color>=0.5f);
}
PT_FN float3 zzz_matcap(const Scene &s,const Hit &h,GPUParams &p,float4 data,float4 other,float3 color) {
    return zzz_matcap_frame(s,h,p.right.xyz(),p.up.xyz(),p.features.w,data,other,color);
}
PT_FN float zzz_depth_sample(const Scene &s,float2 uv,GPUParams &p) {
    int2 xy=i2(clamp(uv,0.0f,1.0f)*f2(p.image.xy()));
    xy=clamp(xy,i2(0),i2(p.image.xy())-1);
    return s.depths[uint(xy.y)*p.image.x+uint(xy.x)];
}
// HoyoToon rim_screen_mask with camera-linear depth prepared by depth_kernel.
PT_FN float zzz_rim_mask(const Scene &s,const Hit &h,GPUParams &p,float3 l,const GPUMaterial &m) {
    if (!s.depths) return 0;
    float3 back=normalize(cross(p.right.xyz(),p.up.xyz())),delta=h.p-p.origin.xyz();
    float depth=-dot(delta,back);
    if (depth<=0.0001f) return 0;
    float focus=length(p.lower_left.xyz()+0.5f*p.horizontal.xyz()+0.5f*p.vertical.xyz()-p.origin.xyz());
    float2 half_size=f2(length(p.horizontal.xyz()),length(p.vertical.xyz()))/(2*focus);
    float2 screen=0.5f+f2(dot(delta,p.right.xyz()),dot(delta,p.up.xyz()))/(2*depth*half_size);
    float range=clamp(2*atan(half_size.y)*180/PI,0.0f,150.0f)/180;
    float width_depth=clamp(1/max(length(delta),0.0001f),0.0f,1.0f)/max(range,0.0001f);
    float width=m.zzz_effects.z*0.0025f*mix(0.5f,0.45f,range)*width_depth;
    float2 offset=screen+width*f2(dot(h.normal,p.right.xyz()),dot(h.normal,p.up.xyz()));
    float difference=zzz_depth_sample(s,offset,p)-zzz_depth_sample(s,screen,p);
    return clamp(difference*dot(h.normal,l)*2.5f,0.0f,1.0f);
}

// Adapted HoyoToon ndotv_rim (GPL-3.0).
PT_NOINLINE float3 zzz_ndotv_rim(float3 normal, float3 view, float3 light, float4 other_data, const GPUMaterial &m)
{
    bool skin_area = int(4-floor(4.5f*other_data.x))==as_type<int>(m.zzz_source.x);
    bool4 color_check = (other_data.xxxx() < f4(0.200000003,0.400000006,0.600000024,0.800000012));
    float4 rim=f4(0);
    float4 r10 = 1;
    float4 r7 = 1;
    float4 r6 = 1;
    float4 r4=f4(0);
    float4 r2=f4(0);
    float4 r3=f4(0);
    float4 r1=f4(0);
    float4 r0 = 1;
    rim.x = dot(view.xyz(), -light.xyz());
    rim.x = pow(-rim.x * 0.5 + 0.5, 2);
    rim.x = pow(rim.x, 2.0);
    rim.y = rim.x * 0.5 + 0.5;
    rim.z = (mix(normal.y, 1.0f, 0.5f)) * 0.5 + 0.5;
    r3.x = pow(rim.z, 2.0);
    rim.z = saturate(skin_area ? rim.z : r3.x);
    r3.x = rim.z * -2 + 3;
    rim.z = rim.z * rim.z;
    rim.z = r3.x * rim.z;
    r3.x = rim.z * rim.z;
    r3.x = r3.x * r3.x;
    r3.x = r3.x * rim.z;
    float3 something;
    something.set_xyz(skin_area ? f3(1,0.5,-1) : f3(0.9,1,-0.9));
    r3.y = something.y + something.z;
    r3.x = r3.x * r3.y + something.x;
    rim.z = r3.x * rim.z;
    r3.x = 1 * 1;
    r3.x = r3.x * 0.949999988 + 0.0500000007;

    r3.y = 1;
    rim.y = rim.y * rim.z;
    rim.y = rim.y * r3.x;
    rim.y = rim.y * r3.y;
    r0.w = sqrt(r0.w);
    rim.z = 0.0833333358 * r0.w;
    rim.z = min(1.0f, rim.z);
    r3.set_xy(rim.zz() * f2(-0.5,-0.5) + f2(0.75,0.5));
    rim.z = dot(view, normal);
    rim.z = 1 + -rim.z;
    r3.set_xy(rim.zz() + -r3.xy());
    r3.set_xy(saturate(f2(5.00000048,3.33333325) * r3.xy()));
    r3.set_zw(r3.xy() * f2(-2,-2) + f2(3,3));
    r3.set_xy(r3.xy() * r3.xy());
    r3.set_xy(r3.zw() * r3.xy());
    rim.z = skin_area ? r3.x : r3.y;
    float3 sun_color;
    sun_color.set_xyz(color_check.w ? m.zzz_sun_colors[1].xyz() : m.zzz_sun_colors[0].xyz());
    sun_color.set_xyz(color_check.z ? m.zzz_sun_colors[2].xyz() : sun_color.xyz());
    sun_color.set_xyz(color_check.y ? m.zzz_sun_colors[3].xyz() : sun_color.xyz());
    sun_color.set_xyz(color_check.x ? m.zzz_sun_colors[4].xyz() : sun_color.xyz());
    sun_color.set_xyz(skin_area ? sun_color.xyz() : f3(dot(sun_color, f3(0.300000012,0.600000024,0.100000001))));
    float grey;
    grey = skin_area ? dot(sun_color, f3(0.330000013,0.330000013,0.330000013)) : dot(sun_color, f3(0.300000012,0.600000024,0.100000001));
    sun_color = pow(sun_color, 8.0f);
    grey = (1.0f/(6.10351563e-005 + dot(sun_color, f3(0.7f)))) * grey;
    sun_color = grey * sun_color + -r7.xyz();
    sun_color = 1 * sun_color + r7.xyz();
    rim.x = pow(rim.x, 20);

    something.set_xyz(r7.xyz() + -sun_color);
    sun_color = rim.xxx() * something.xyz() + sun_color;
    rim.x = 1;
    r1.z = 0 * 1 + rim.x;
    r1.z = 0.330000013 * r1.z;
    r1.z = r1.z * r1.z;
    r1.z = r1.z * -0.199999988 + 1;
    rim.x = 1;
    r1.z = 0.100000001 * r1.z;

    something.set_xyz(1.0f);

    something.set_xyz(normalize(something.xyz()));
    r6.set_xyz(something.xyz() * r1.zzz());
    something.set_xyz(-r1.zzz() * something.xyz() + r10.xyz());
    something.set_xyz(r0.xxx() * something.xyz() + r6.xyz());
    something.set_xyz(something.xyz() * rim.xxx());
    r0.x = rim.y * rim.z;
    rim.set_xyz(r0.xxx() );
    rim.set_xyz(rim.xyz() * something.xyz());
    float3 rg_color;
    rg_color.set_xyz(color_check.w ? m.zzz_rim_colors[1].xyz() : m.zzz_rim_colors[0].xyz());
    rg_color.set_xyz(color_check.z ? m.zzz_rim_colors[2].xyz() : rg_color.xyz());
    rg_color.set_xyz(color_check.y ? m.zzz_rim_colors[3].xyz() : rg_color.xyz());
    rg_color.set_xyz(color_check.x ? m.zzz_rim_colors[4].xyz() : rg_color.xyz());
    sun_color.set_xyz(rg_color.xyz() * sun_color.xyz());
    r0.x = saturate(r0.w * 0.200000003 + -1);
    r0.x = r0.x * -0.699999988 + 1;
    rim.set_xyw(rim.xyz() * r0.xxx());
    r0.w = rim.x + rim.y;
    r0.x = rim.z * r0.x + r0.w;
    r0.x = 0.330000013 * r0.x;
    r0.x = r0.x * r0.x;
    r0.x = r0.x * 0.5 + 1;
    rim.set_xyz(rim.xyw() * r0.xxx());
    r0.x = 1;
    rim.set_xyz(rim.xyz() * r0.xxx() * sun_color);



    return rim.xyz();
}

// Metal adaptation: actual hair geometry casts rays onto the face. This replaces
// Unity's shifted HairShadow/stencil pass, rather than pretending to reproduce it.
// Skin/body/face triangles never cast into this pass: no skin self-intersection.
PT_FN float zzz_hair_visibility(const Scene &s, const Hit &face,
                                 float3 direction,float max_distance,float cone) {
    float3 axis=abs(direction.y)<0.95f ? f3(0,1,0) : f3(1,0,0);
    float3 u=normalize(cross(direction,axis)),v=cross(direction,u);
    const float2 taps[4]={f2(-0.5f,-0.5f),f2(0.5f,-0.5f),f2(-0.5f,0.5f),f2(0.5f,0.5f)};
    float visibility=0;
    for (uint i=0;i<4;++i) {
        float3 d=normalize(direction+cone*(u*taps[i].x+v*taps[i].y));
        Ray ray={face.p+0.0002f*face.geometric,d};
        float t_min=0.0001f,transmittance=1;Hit blocker;
        for (uint step=0;step<16;++step) {
            if (!world_hit(s,ray,t_min,max_distance,blocker,4)) break;
            float alpha=clamp(mmd_base(s,blocker).w,0.0f,1.0f);
            transmittance*=1-alpha;
            if (transmittance<0.001f) {transmittance=0;break;}
            t_min=blocker.t+0.0001f;
        }
        visibility+=transmittance;
    }
    return visibility*0.25f;
}
PT_FN float3 shade_zzz_face(const Scene &s,const Hit &h,
                              GPUParams &p,float3 base) {
    const GPUMaterial &m=s.materials[h.material];
    // Existing packages without an explicit face map keep their previous look.
    if (!m.zzz_face_image.w) return base*m.zzz_color[0].xyz();
    float3 l=zzz_light(h.p,p);float4 fv=zzz_face_values(s,h,l,m);
    float id=zzz_face_material_id(s,h);
    uint region=id<0.6f ? 2u : id<0.8f ? 1u : 0u;
    float3 shadow=f3(1);float face_lit=1;
    if (m.mmd.y>0 && !(p.features.w&16u) && m.zzz_face_image.w) {
        float4 tex=mmd_texture(s,m.zzz_face_image,fv.xy(),false,true);
        float sdf=tex.r*0.9f+0.1f;
        float ao=fv.z<0.5f ? 1.0f : tex.a;
        float lit=smoothstep(fv.w-0.5f,fv.w+0.5f,sdf)*ao;
        face_lit=lit;
        shadow=zzz_shadow_face(id,lit*2-1,fv.z,m);
    }
    if (p.features.w&64u) return f3(face_lit);
    float3 view=normalize(p.origin.xyz()-h.p);
    if (m.mmd.y>0 && m.zzz_face_detail.w>0 && !(p.features.w&512u)) base*=zzz_nose_line(s,h,view,region,m);
    float3 color=clamp(base*shadow,0.0f,1.0f)*m.zzz_color[region].xyz();
    if (m.mmd.y>0 && !(p.features.w&32u) && fv.z>=0.5f) {
        float distance=p.toon_light.w>0 ? 5.0f : length(LIGHT_CENTER-h.p);
        float visible=zzz_hair_visibility(s,h,l,distance,m.zzz_face_shadow.w);
        if (p.features.w&128u) return f3(visible);
        color*=mix(m.zzz_face_shadow.xyz(),f3(1),visible);
    }
    else if (p.features.w&128u) return f3(1);
    if (m.mmd.y>0 && m.zzz_face_detail.w>0 && !(p.features.w&256u)) color+=f3(zzz_face_high(s,h,fv,l,view,m));
    if (m.mmd.y>0 && m.zzz_face_detail.w>0 && m.zzz_effects.y>0 && !(p.features.w&8192u)) {
        float mask=zzz_rim_mask(s,h,p,l,m);
        if (p.features.w&65536u) return f3(mask);
        color+=zzz_ndotv_rim(h.normal,view,l,f4(id,0,0,1),m)*1.5f*mask;
    }
    return color;
}

PT_FN uint zzz_region_id(float id) {
    return id<0.2f ? 4u : id<0.4f ? 3u : id<0.6f ? 2u : id<0.8f ? 1u : 0u;
}
PT_FN float3 shade_zzz(const Scene &s,const Hit &h,GPUParams &p,float3 base)
{
    const GPUMaterial &m=s.materials[h.material];
    uint type=uint(m.zzz_misc.y);
    if (type==1) return shade_zzz_face(s,h,p,base);
    if (type==2) return zzz_eye_color(s,h,base,m);
    float2 uv=surface_uv(s,h);
    float4 light=mmd_texture(s,m.normal_image,uv,false,true);
    float4 data=mmd_texture(s,m.metallic_image,uv,false,true);
    float4 other=mmd_texture(s,m.roughness_image,uv,false,true);
    if (!m.normal_image.w) light=f4(0.5f,0.5f,0.5f,1);
    if (!m.metallic_image.w) data=f4(1,0,0,1);
    if (!m.roughness_image.w) other=f4(1,0,0,1);
    if (p.features.w&1u) light.z=0.5f;
    if (p.features.w&2u) data.x=1;
    if (p.features.w&4u) data.z=1;
    if (p.features.w&8u) data.y=0;
    if (m.zzz_face_detail.w>0 && (p.features.w&16384u)) return f3(mmd_base(s,h).a);
    if (m.zzz_face_detail.w>0 && (p.features.w&32768u)) return f3(other.b);
    uint region=zzz_region_id(data.x);
    float3 l=zzz_light(h.p,p),v=normalize(p.origin.xyz()-h.p);
    // The N.B diffuse bias participates in the original multi-band Toon shadow.
    // Geometric self-shadowing is kept separate for the current first-stage pass.
    float4 shadow=m.mmd.y>0 ? zzz_shadow_body(h.normal,l,light,data.x,1,m) : f4(1,1,1,0);
    float3 color=clamp(base*shadow.xyz(),0.0f,1.0f)*m.zzz_color[region].xyz();
    color=zzz_matcap(s,h,p,data,other,color);
    if (p.features.z && !(type==4 && p.settings.w)) {
        float3 spec=zzz_specular(h.normal,v,l,data,other,color,shadow.w,h.p,m);
        // Original Material_Detect marks ID >= .8 as skin.
        if (int(4-floor(4.5f*data.x))==0) spec*=0.1f;
        color+=spec;
    }
    if (m.mmd.y>0 && m.zzz_face_detail.w>0 && m.zzz_effects.y>0 && !(p.features.w&8192u)) {
        float mask=zzz_rim_mask(s,h,p,l,m);
        if (p.features.w&65536u) return f3(mask);
        color+=zzz_ndotv_rim(h.normal,v,l,data,m)*1.5f*mask;
    }
    return max(color,f3(0));
}
PT_FN float3 shade_mmd(const Scene &s, const Hit &hit,
                        GPUParams &p, float3 base)
{
    const GPUMaterial &m=s.materials[hit.material];
    if (m.zzz_misc.w>0) return shade_zzz(s,hit,p,base);
    float3 delta=LIGHT_CENTER-hit.p;
    float distance=length(delta);
    float3 light=delta/max(distance,0.0001f);
    float facing=clamp(0.5f+0.5f*dot(hit.normal,light),0.0f,1.0f);
    float3 ramp=f3(1);
    if (m.toon_image.w && m.mmd.y>0) {
        // Bright at the top of the original ramp, shadow at the bottom.
        // A toon ramp is a light lookup, not a texture sampled with the mesh UV.
        float3 lit=mmd_texture(s,m.toon_image,f2(0.5f,facing),true,false).xyz();
        // Version 1 uses the normal/light lookup. Model self-shadowing is not
        // folded into this ramp: overlapping MMD body layers need a separate
        // shadow policy to avoid triangle-shaped shading on skin.
        ramp=mix(f3(1),lit,m.mmd.y);
    }
    float3 color=surface_reflection(s,hit,p,base,base*ramp);
    if (m.sphere_image.w && m.mmd_flags.x) {
        // View-space normal maps to [0,1]^2: camera rotation moves the highlight.
        float2 sphere_uv=0.5f+0.5f*f2(dot(hit.normal,p.right.xyz()),dot(hit.normal,p.up.xyz()));
        // MMD additive sphere maps are linear, multiplicative sphere maps sRGB.
        float3 sphere=mmd_texture(s,m.sphere_image,sphere_uv,m.mmd_flags.x==1,false).xyz()*m.mmd.z;
        color=m.mmd_flags.x==2 ? color+sphere : color*sphere;
    }
    return color;
}
PT_FN bool mmd_visible_hit(const Scene &s, Ray ray, GPURandom &rng, Hit &hit)
{
    float minimum=0.00001f;
    for (uint layer=0;layer<64;++layer) {
        if (!world_hit(s,ray,minimum,10000.0f,hit)) return false;
        const GPUMaterial &m=s.materials[hit.material];
        if (m.flags.x!=4 || random_uniform(rng)<mmd_base(s,hit).w) return true;
        // Stochastic alpha keeps partial opacity correct over accumulated samples.
        minimum=hit.t+0.00001f/max(length(ray.direction),0.00001f);
    }
    return false;
}
PT_FN float power_heuristic(float a, float b)
{
    float scale = max(a, b);
    if (scale <= 0) return 0;
    a /= scale; b /= scale;
    return a*a/(a*a+b*b);
}
PT_FN float light_pdf(float3 p, float3 q)
{
    float3 delta = q-p;
    float d2 = dot(delta, delta);
    if (d2 <= 0) return 0;
    float cosine = abs(dot(normalize(q-LIGHT_CENTER), -delta/sqrt(d2)));
    return cosine > 0 ? d2/(cosine*4*PI*LIGHT_RADIUS*LIGHT_RADIUS) : 0;
}
// Choose lights by emitted power, then sample a uniform point on the rectangle.
// The exact selection probability also enters BSDF-hit MIS; zero-power lights
// retain geometry but have no probability of wasting a direct-light sample.
PT_FN float area_power(GPUAreaLight &a) {
    return dot(a.emission.xyz(),f3(.2126f,.7152f,.0722f))*a.center.w;
}
PT_FN float total_area_power(GPUParams &p) {
    float total=0;for (uint i=0;i<p.rendering.x;++i) total+=area_power(p.area_lights[i]);return total;
}
PT_FN float area_pdf(GPUParams &p,uint id,float3 from,float3 to) {
    if (id>=p.rendering.x || !p.rendering.x) return 0;
    GPUAreaLight &a=p.area_lights[id];
    float3 delta=to-from;float d2=dot(delta,delta);
    if (d2<1e-12f) return 0;
    float3 normal=normalize(cross(a.u.xyz(),a.v.xyz()));
    float cosine=dot(normal,-delta/sqrt(d2));
    float total=total_area_power(p);
    return cosine>0 && total>0 ? d2/(cosine*a.center.w)*(area_power(a)/total) : 0.0f;
}
PT_FN bool sample_area(GPUParams &p,float3 from,GPURandom &rng,
                        uint &id,float3 &l,float3 &Le,float &distance,float &pdf) {
    if (!p.rendering.x) return false;
    float total=total_area_power(p);if (total<=0) return false;
    float pick=random_uniform(rng)*total;id=p.rendering.x-1;
    for (uint i=0;i<p.rendering.x;++i) {
        pick-=area_power(p.area_lights[i]);if (pick<=0) {id=i;break;}
    }
    GPUAreaLight &a=p.area_lights[id];
    float u=2*random_uniform(rng)-1,v=2*random_uniform(rng)-1;
    float3 point=a.center.xyz()+a.u.xyz()*u+a.v.xyz()*v,delta=point-from;
    distance=length(delta);if (distance<.002f) return false;
    l=delta/distance;pdf=area_pdf(p,id,from,point);Le=a.emission.xyz();
    return pdf>0 && any(Le>0);
}
PT_FN float3 diffuse_area_direct(const Scene &s,const Hit &h,float3 albedo,
                                 GPURandom &rng,GPUParams &p,bool continuation) {
    uint id;float3 l,Le;float distance,pdf;
    if (!sample_area(p,h.p,rng,id,l,Le,distance,pdf) || dot(h.geometric,l)<=0) return f3(0);
    float cosine=max(dot(h.normal,l),0.0f);
    return albedo/PI*Le*(cosine/pdf)*mmd_shadow(s,h,l,distance,int(id))*
           (continuation ? power_heuristic(pdf,cosine/PI) : 1.0f);
}
PT_FN float3 direct_light(const Scene &s, const Hit &rec,
                           float3 albedo, GPURandom &rng, bool mmd_scene, bool continuation=true)
{
    const float epsilon = 0.001f;
    float3 normal = random_unit(rng);
    float3 point = LIGHT_CENTER+LIGHT_RADIUS*normal;
    float3 delta = point-rec.p;
    float d2 = dot(delta, delta);
    if (d2 <= 4*epsilon*epsilon) return f3(0);
    float distance = sqrt(d2);
    float3 wi = delta/distance;
    float cosine = max(dot(rec.normal, wi), 0.0f);
    if (cosine <= 0 || abs(dot(normal, -wi)) <= 0) return f3(0);
    if (s.camera->extensions.z && dot(normal,-wi)<=0) return f3(0);
    Hit blocker;
    float visibility=1;
    if (mmd_scene) visibility=mmd_shadow(s,rec,wi,distance);
    else if (world_hit(s, Ray{rec.p, wi}, epsilon, distance-epsilon, blocker)) return f3(0);
    if (visibility<=0) return f3(0);
    float pdf = light_pdf(rec.p, point);
    if (pdf <= 0) return f3(0);
    float weight = continuation ? power_heuristic(pdf, cosine/PI) : 1.0f;
    return visibility*(albedo/PI)*LIGHT_EMISSION*(cosine/pdf)*weight;
}
PT_FN float schlick_probability(float cosine, float ior)
{
    float base = (1-ior)/(1+ior); base *= base;
    float x = 1-cosine;
    return base+(1-base)*x*x*x*x*x;
}

// The same BSDF is evaluated by direct-light sampling and by every continued path.
// Toon is an artistic directional reflectance, not camera color added as emission.
struct CharacterBSDF {
    float3 base, tangent, shadow_tint, shallow_tint, spec_tint, head_forward, head_right;
    float2 face_sdf;
    float metallic, roughness, spec_probability, ax, ay, spec_mask;
    float4 bands;
    float2 face_levels;
    bool toon, face, hair, source, source_tangent_valid;
    uint source_disabled;
    Scene source_scene;
    Hit source_hit;
    float3 source_view_origin;
    const GPUMaterial *source_material;
    float3 source_position;
    float4 source_light,source_data,source_other,source_face_left,source_face_right;
    float2 source_uv_left,source_uv_right;
    float source_face_flag,source_face_id,source_nose_alpha;
};
PT_FN CharacterBSDF character_bsdf(const Scene &s,const Hit &h,GPUParams &p) {
    const GPUMaterial &m=s.materials[h.material];
    CharacterBSDF b; b.base=clamp(mmd_base(s,h).xyz(),0.0f,0.98f);
    b.toon=p.extensions.z==2 && !p.extensions.w;
    b.source=b.toon && (p.rendering.w&1u) && m.zzz_misc.w>0;
    b.source_disabled=p.features.w;
    b.bands=f4(.30f,.65f,.12f,.55f); b.face_levels=f2(.18f,.65f);
    b.face=false; b.hair=m.zzz_misc.w>0 && uint(m.zzz_misc.y)==4;
    b.source_tangent_valid=false;
    b.face_sdf=f2(0); b.head_forward=m.zzz_head_forward.xyz(); b.head_right=m.zzz_head_right.xyz();
    b.shadow_tint=f3(1); b.shallow_tint=f3(1); b.spec_tint=f3(1); b.spec_mask=1;
    float3 axis=abs(h.normal.y)<0.95f ? f3(0,1,0) : f3(1,0,0);
    b.tangent=normalize(cross(axis,h.normal));
    const GPUPrimitive &g=s.primitives[h.primitive];
    if (g.meta.w&1u) {
        float3 t=(h.bary.x*g.tangent0+h.bary.y*g.tangent1+h.bary.z*g.tangent2).xyz();
        t-=h.normal*dot(h.normal,t);
        if (dot(t,t)>1e-12f) {b.tangent=normalize(t);b.source_tangent_valid=true;}
    }
    b.metallic=0; b.roughness=0.6f;
    float2 uv=surface_uv(s,h);
    if (b.source) {
        b.base=max(mmd_base(s,h).xyz(),f3(0));
        b.source_scene=s;b.source_hit=h;
        b.source_material=&m;b.source_position=h.p;
        // Missing controls use HoyoToon's declared texture defaults, not guessed images:
        // _LightTex=linearGray, _OtherDataTex and _OtherDataTex2=white.
        float2 light_uv=zzz_light_uv(s,h);
        b.source_light=m.normal_image.w ? mmd_texture(s,m.normal_image,light_uv,false,true) : f4(.5f);
        b.source_data=m.metallic_image.w ? mmd_texture(s,m.metallic_image,light_uv,false,true) : f4(1);
        b.source_other=m.roughness_image.w ? mmd_texture(s,m.roughness_image,light_uv,false,true) : f4(1);
        if(m.zzz_uv_rules.z) b.source_other.set_xyz(f3(mmd_texture(s,m.image,uv,false,true).w,b.source_light.w,b.source_data.w));
        if (p.features.w&1u) b.source_light.b=.5f;
        if (p.features.w&2u) b.source_data.r=1;
        if (p.features.w&4u) b.source_data.b=0;
        if (!p.features.z || (p.features.w&8u)) b.source_data.g=0;
        if (uint(m.zzz_misc.y)==2 && m.zzz_render.w>=3 && m.zzz_eye_lut.w) {
            float red=dot(h.bary,f3(g.vertex_color0.r,g.vertex_color1.r,g.vertex_color2.r));
            uint code=uint(clamp(red*255,0.0f,255.0f));
            b.base*=mmd_texel(s,m.zzz_eye_lut,i2(code&15u,code>>4),false).xyz()*2;
        }
        b.source_face_id=zzz_face_material_id(s,h);b.source_face_flag=0;
        b.source_nose_alpha=mmd_texture(s,m.image,uv,false,true).a;
        if (uint(m.zzz_misc.y)==1 && m.zzz_face_image.w && !(p.features.w&16u)) {
            float4 left=zzz_face_values(s,h,-m.zzz_head_right.xyz(),m);
            float4 right=zzz_face_values(s,h,m.zzz_head_right.xyz(),m);
            b.source_uv_left=left.xy();b.source_uv_right=right.xy();b.source_face_flag=left.z;
            b.source_face_left=mmd_texture(s,m.zzz_face_image,left.xy(),false,true);
            b.source_face_right=mmd_texture(s,m.zzz_face_image,right.xy(),false,true);
            b.face=true;
        }
        b.metallic=0;b.roughness=1;b.spec_probability=0;b.ax=b.ay=1;
        return b; // No authored bands, lobe, tint, hair metallic override or roughness floor.
    }
    if (p.features.z && m.zzz_misc.w>0 && uint(m.zzz_misc.y)!=1 && uint(m.zzz_misc.y)!=2) {
        float4 packed=mmd_texture(s,m.metallic_image,uv,false,true);
        float4 control=mmd_texture(s,m.roughness_image,uv,false,true);
        if (m.metallic_image.w && !(p.features.w&8u)) b.metallic=clamp(m.zzz_params.x*packed.y,0.0f,1.0f);
        if (m.roughness_image.w) b.roughness=clamp(1-control.y*m.zzz_params.y,0.12f,1.0f);
        if (uint(m.zzz_misc.y)==4) { b.metallic=0; b.roughness=max(b.roughness,0.35f); }
    } else if (p.features.z && m.zzz_misc.w<=0 && m.surface.w>0) {
        b.metallic=m.surface.x; b.roughness=m.surface.y;
        if (m.metallic_image.w) b.metallic*=mmd_texture(s,m.metallic_image,uv,false,m.flags.y==0)[m.surface_flags.x];
        if (m.roughness_image.w) b.roughness*=mmd_texture(s,m.roughness_image,uv,false,m.flags.y==0)[m.surface_flags.y];
    }
    b.metallic=clamp(b.metallic,0.0f,1.0f); b.roughness=clamp(b.roughness,0.12f,1.0f);
    b.ax=b.ay=clamp(0.12f+0.5f*b.roughness,0.12f,0.65f);
    if (b.toon && m.zzz_misc.w>0) {
        float4 packed=mmd_texture(s,m.metallic_image,uv,false,true);
        uint region=(p.features.w&2u) || !m.metallic_image.w ? 0u : zzz_region_id(clamp(packed.r,0.0f,1.0f));
        b.base*=clamp(m.zzz_color[region].xyz(),0.0f,1.0f);
        b.shadow_tint=clamp(m.zzz_shadow[region].xyz(),0.0f,1.0f);
        b.shallow_tint=clamp(m.zzz_shallow[region].xyz(),0.0f,1.0f);
        b.spec_tint=clamp(m.zzz_specular[region].xyz(),0.0f,1.0f);
        b.spec_mask=clamp(m.zzz_params.z*10,0.0f,1.0f);
        if (m.metallic_image.w && !(p.features.w&4u)) b.spec_mask*=packed.b;
        if (p.rendering.z) {
            GPUToonProfile profile=m.toon_profiles[region];
            float metal_mix=smoothstep(0.35f,0.80f,b.metallic);
            profile.bands=mix(profile.bands,m.toon_metal.bands,metal_mix);
            profile.lobe=mix(profile.lobe,m.toon_metal.lobe,metal_mix);
            profile.tint=mix(profile.tint,m.toon_metal.tint,metal_mix);
            b.bands=profile.bands;b.face_levels=b.bands.zw();b.roughness=max(b.roughness,profile.lobe.w);
            float width=.12f+.5f*b.roughness;
            b.ax=clamp(width*profile.lobe.x,.05f,.90f);b.ay=clamp(width*profile.lobe.y,.05f,.90f);
            b.spec_mask*=profile.lobe.z;
            b.shadow_tint=mix(f3(1),b.shadow_tint,profile.tint.w)*profile.tint.xyz();
            b.shallow_tint=mix(f3(1),b.shallow_tint,profile.tint.w)*profile.tint.xyz();
        }
        // Original strand tangent defines the highlighter's long/short axes.
        if (uint(m.zzz_misc.y)==4 && !p.rendering.z) { b.ax=0.50f; b.ay=0.09f; }
        if (uint(m.zzz_misc.y)==1) {
            if (!p.rendering.z) b.spec_mask*=0.15f;
            if (m.zzz_face_image.w && !(p.features.w&16u)) {
                float4 left=zzz_face_values(s,h,-b.head_right,m),right=zzz_face_values(s,h,b.head_right,m);
                b.face=left.z>=0.5f;
                b.face_sdf=f2(mmd_texture(s,m.zzz_face_image,left.xy(),false,true).r,
                                 mmd_texture(s,m.zzz_face_image,right.xy(),false,true).r)*0.9f+0.1f;
            }
        }
        if (uint(m.zzz_misc.y)==2 && !p.rendering.z) { b.ax=b.ay=0.16f; b.spec_mask=1; }
        if (uint(m.zzz_misc.y)==4 && p.settings.w==2u) b.spec_mask=0;
    }
    b.spec_probability=mix(0.15f,0.9f,b.metallic);
    return b;
}
// Projected elliptical NDFs integrate D(h)*cos(theta_h) dOmega to one.
// Hair tapers continuously to zero at the rim; other Toon materials keep their hard support.
PT_FN float toon_ndf(CharacterBSDF b,float3 n,float3 h) {
    float x=dot(h,b.tangent)/b.ax,y=dot(h,cross(n,b.tangent))/b.ay;
    float radius_squared=x*x+y*y;
    if (dot(n,h)<=0 || radius_squared>=1) return 0.0f;
    return (b.hair ? 2*(1-radius_squared) : 1)/(PI*b.ax*b.ay);
}
// Integral of mu/max(mu,.12): analytic normalization for editable bands.
PT_FN float band_integral(float x) { return x<=.12f ? x*x/.24f : x-.06f; }
// Source angular response is evaluated for every incident/outgoing direction pair.
// HoyoToon is a raster shader, not a supplied reciprocal BRDF. The explicit adapter
// f=R/(2*pi*cos(theta_i)) makes the path integral an average of R over the hemisphere.
// These are integration factors; they do not rewrite source material parameters.
// No response is added as emission, MatCap overlay or final camera color.
// Ray equivalent of the upstream offset-depth rim mask. Each outgoing path
// supplies its own origin/view frame, so this is evaluated inside the scattering
// response at every bounce rather than painted onto the camera image.
PT_NOINLINE float source_rim_mask(const CharacterBSDF &b,float3 n,float3 v,float3 l) {
    const GPUMaterial &m=*b.source_material;
    float distance=length(b.source_hit.p-b.source_view_origin);
    float cosine=saturate(dot(n,l));
    // Exact zero-support thresholds of upstream ndotv_rim: avoid a BVH probe
    // where its angular factor will be zero, without altering rim appearance.
    float id=uint(m.zzz_misc.y)==1 ? b.source_face_id : b.source_data.r;
    bool skin=int(4-floor(4.5f*id))==as_type<int>(m.zzz_source.x);
    if (dot(n,v)>=(skin ? .291666667f : .541666667f)) return 0;
    if (cosine<=0 || distance<=.0001f || m.zzz_effects.z<=0) return 0;
    GPUParams &p=*b.source_scene.camera;
    float focus=length(p.lower_left.xyz()+.5f*p.horizontal.xyz()+.5f*p.vertical.xyz()-p.origin.xyz());
    float tan_half=length(p.vertical.xyz())/(2*focus),fov=2*atan(tan_half);
    float range=clamp(fov*180/PI,0.0f,150.0f)/180;
    // HoyoToon width_depth = saturate(1/distance)/fov_range, not distance/range.
    float width=m.zzz_effects.z*.0025f*mix(.5f,.45f,range)*saturate(1/distance)/max(range,1e-6f);
    float3 right=cross(p.up.xyz(),v);
    right=dot(right,right)>1e-12f ? normalize(right) : p.right.xyz();
    float3 up=normalize(cross(v,right));
    float2 offset=width*f2(dot(n,right),dot(n,up));
    float aspect=float(p.image.x)/p.image.y;
    Ray probe{b.source_view_origin,normalize(-v+2*tan_half*(right*offset.x*aspect+up*offset.y))};
    float minimum=.00001f,other_depth=10000;Hit h;
    for (uint layer=0;layer<64;++layer) {
        if (!world_hit(b.source_scene,probe,minimum,10000,h)) break;
        if (b.source_scene.materials[h.material].flags.x!=4 || mmd_base(b.source_scene,h).a>=.5f) {
            other_depth=dot(h.p-b.source_view_origin,-v);break;
        }
        minimum=h.t+.00001f;
    }
    return saturate(max(other_depth-distance,0.0f)*cosine*2.5f);
}
// Keep this shared routine out of repeated inlining into every light/sampling
// branch. This also avoids excessive Metal pipeline optimization at startup.
// Authored interpretation of the two source nose thresholds as cosines.
// A nose line fades around the yaw threshold and when the viewer is above
// the head (equivalent to the head looking down). Head axes are world-space.
// Evaluate using this path's outgoing direction, including indirect bounces.
// The original D.a mask still determines WHICH pixels can contain a nose line.
PT_FN float designed_nose_visibility(const GPUMaterial &m,float3 v)
{
    if(!(uint(m.zzz_designed.w)&2u)) return 1;
    float3 forward=m.zzz_head_forward.xyz(),right=m.zzz_head_right.xyz();
    float3 up=normalize(cross(forward,right));
    float f=dot(v,forward),r=dot(v,right),u=dot(v,up);
    if(f<=0) return 0;
    float yaw_cos=f/sqrt(max(f*f+r*r,1e-12f));
    float pitch_cos=f/sqrt(max(f*f+u*u,1e-12f));
    // Our authored transition half-width is .05 in cosine space.
    float horizontal=smoothstep(max(0.0f,m.zzz_designed.y-.05f),min(1.0f,m.zzz_designed.y+.05f),yaw_cos);
    float down=u>0 ? smoothstep(max(0.0f,m.zzz_designed.z-.05f),min(1.0f,m.zzz_designed.z+.05f),pitch_cos) : 1;
    return horizontal*down;
}
PT_NOINLINE float3 source_toon_response(const CharacterBSDF &b,float3 n,float3 v,float3 l) {
    const GPUMaterial &m=*b.source_material;
    uint type=uint(m.zzz_misc.y);
    if (b.source_hit.outline) {
        uint id=type==1 ? min(zzz_region_id(b.source_face_id),2u) : zzz_region_id(b.source_data.r);
        float3 tint=saturate(pow(max(m.zzz_outline[id].xyz(),f3(0))*.5f,f3(1.5f)));
        float red=saturate(dot(-n,l))*.5f+.5f;
        return b.base*tint*mix(.01f,1.0f,red);
    }
    if (type==2 || type==3) return max(b.base*m.zzz_color[0].xyz(),f3(0));
    if (type==1) {
        uint region=zzz_region_id(b.source_face_id);
        float3 shadow=f3(1);
        float3 base=b.base;
        float ndotv=pow(saturate(dot(n,v)),10.0f);
        float amount=(b.source_disabled&512) ? 0 : saturate(smoothstep(m.zzz_face_detail.y,m.zzz_face_detail.z,ndotv)-b.source_nose_alpha);
        if(!(b.source_disabled&1048576u)) amount*=designed_nose_visibility(m,v);
        float3 ink=pow(max(m.zzz_outline[min(region,2u)].xyz(),f3(0))*.5f,f3(2.1f));
        base*=mix(f3(1),ink,amount);
        float highlight=0;
        if (b.face) {
            bool right=dot(m.zzz_head_right.xz(),l.xz())>0;
            float4 tex=right ? b.source_face_right : b.source_face_left;
            float2 uv=right ? b.source_uv_right : b.source_uv_left;
            float threshold=1-(dot(m.zzz_head_forward.xz(),l.xz())*.5f+.5f);
            float sdf=tex.r*.9f+.1f;
            float lit=smoothstep(threshold-.5f,threshold+.5f,sdf)*(b.source_face_flag<.5f ? 1 : tex.a);
            shadow=zzz_shadow_face(b.source_face_id,lit*2-1,b.source_face_flag,m);
            if (!(b.source_disabled&256) && b.source_face_flag>=.5f && uv.x>.45f && uv.x<.55f) {
                float ndoth=pow(saturate(dot(n,normalize(l+v))),10.0f);
                float mask=saturate(tex.g-.5f);threshold=max(threshold,.75f);
                highlight=smoothstep(threshold-.75f,threshold+.75f,mask)*ndoth*10*m.zzz_face_detail.x;
            }
        }
        float3 face=saturate(base*shadow)*m.zzz_color[region].xyz()+f3(highlight);
        if (m.zzz_effects.y>0 && !(b.source_disabled&8192u))
            face+=1.5f*source_rim_mask(b,n,v,l)*zzz_ndotv_rim(n,v,l,f4(b.source_face_id),m);
        return max(face,f3(0));
    }
    uint region=zzz_region_id(b.source_data.r);
    // Self-shadow visibility belongs to traced transport, not to a camera-specific mask.
    float4 shadow=zzz_shadow_body(n,l,b.source_light,b.source_data.r,1,m);
    float3 color=saturate(b.base*shadow.xyz());
    float3 spec=f3(0);
    // A white fallback specular mask would fabricate highlights on the whole mesh.
    // Until both controls are explicitly bound, omit that feature rather than invent it.
    if (m.zzz_source.y>0 && m.metallic_image.w && m.roughness_image.w && b.source_data.b>0) {
        float anisotropy=b.hair && b.source_tangent_valid && (uint(m.zzz_designed.w)&1u) && !(b.source_disabled&524288u)
            ? m.zzz_designed.x : 0;
        spec=zzz_specular(n,v,l,b.source_data,b.source_other,color,shadow.w,b.source_position,m,b.tangent,anisotropy);
        // Keep upstream Material_Detect's integer-bit test; do not assume region zero is skin.
        if (int(4-floor(4.5f*b.source_data.r))==as_type<int>(m.zzz_source.x)) spec*=.1f;
    }
    // MatCap colours are directional reflectance data, never camera emission.
    // Use a view frame belonging to this outgoing path direction at EVERY bounce.
    float3 right=cross(b.source_scene.camera->up.xyz(),v);
    right=dot(right,right)>1e-12f ? normalize(right) : b.source_scene.camera->right.xyz();
    float3 up=normalize(cross(v,right));
    color=zzz_matcap_frame(b.source_scene,b.source_hit,right,up,b.source_disabled,b.source_data,b.source_other,color);
    color+=spec;
    // Source ps_model applies material_color after specular, unlike the old local adapter.
    color*=m.zzz_color[region].xyz();
    if (m.zzz_effects.y>0 && !(b.source_disabled&8192u))
        color+=1.5f*source_rim_mask(b,n,v,l)*zzz_ndotv_rim(n,v,l,b.source_data,m);
    return max(color,f3(0));
}
// Source emission is carried as actual emitted radiance, so other surfaces can
// receive it via traced paths. It is not added by the display/camera compositor.
// Body formula's colour selection uses M.B, as in upstream emission(), not M.R.
PT_FN float3 source_emission(const CharacterBSDF &b) {
    if (!b.source || b.source_hit.outline || (b.source_disabled&262144u)) return f3(0);
    const GPUMaterial &m=*b.source_material;
    if (m.zzz_emission_flags.x<=0 || !m.roughness_image.w) return f3(0);
    float mask=b.source_other.b;
    if (m.zzz_source.w>0) mask=saturate(1.25f*(mask-.2f));
    return max(b.base*m.zzz_emission[zzz_region_id(b.source_data.b)].xyz()*mask,f3(0));
}
// The inverted outline shell is viewed from its back. Treat it as a thin,
// absorbing ink sheet with reflection AND transmission, so exterior light can
// reach its back instead of being discarded by a reflection-only hemisphere.
// This is a local transport model, not a recovered HoyoToon BSDF.
PT_FN bool source_ink(const CharacterBSDF &b) { return b.source && b.source_hit.outline; }
PT_FN float character_cosine(const CharacterBSDF &b,float3 n,float3 l) {
    return source_ink(b) ? abs(dot(n,l)) : max(dot(n,l),0.0f);
}
PT_FN bool character_support(const CharacterBSDF &b,const Hit &h,float3 l) {
    return source_ink(b) || (dot(h.geometric,l)>0 && dot(h.normal,l)>0);
}
PT_FN float3 character_eval(const CharacterBSDF &b,float3 n,float3 v,float3 l,float &pdf) {
    pdf=0; float nv=dot(n,v),nl=dot(n,l);
    if (nv<=0) return f3(0);
    if (source_ink(b)) {
        if (abs(nl)<1e-8f) return f3(0);
        pdf=1/(4*PI);
        return source_toon_response(b,n,v,l)/(2*PI*abs(nl));
    }
    if (nl<=0 || dot(v+l,v+l)<1e-12f) return f3(0);
    if (b.source) {
        pdf=1/(2*PI);
        return source_toon_response(b,n,v,l)/(2*PI*nl);
    }
    float3 h=normalize(v+l); float nh=max(dot(n,h),0.0f),vh=max(dot(v,h),1e-6f);
    float a=b.roughness*b.roughness,a2=a*a,den=nh*nh*(a2-1)+1;
    float D=b.toon ? toon_ndf(b,n,h) : a2/(PI*den*den);
    float G=0.5f/max(nl*sqrt(nv*nv*(1-a2)+a2)+nv*sqrt(nl*nl*(1-a2)+a2),1e-6f);
    float3 f0=mix(f3(0.04f),b.base,b.metallic),F=f0+(1-f0)*pow(1-vh,5.0f);
    pdf=(1-b.spec_probability)*nl/PI+b.spec_probability*D*nh/(4*vh);
    float3 diffuse=b.base/PI;
    if (b.toon) {
        // Three finite bands: the division cancels cosine lighting away from grazing.
        // Normalization is 2*integral_0^1 mu*q(mu)/max(mu,.12) dmu. Tints <=1 cannot
        // increase that diffuse energy bound. No radiance is created in these bands.
        float q=nl<b.bands.x ? b.bands.z : nl<b.bands.y ? b.bands.w : 1.0f;
        float3 tint=nl<b.bands.x ? b.shadow_tint : nl<b.bands.y ? b.shallow_tint : f3(1);
        float j1=band_integral(b.bands.x),j2=band_integral(b.bands.y);
        float normalization=2*(b.bands.z*j1+b.bands.w*(j2-j1)+band_integral(1)-j2);
        float3 band_response=q*tint;
        if (b.hair) {
            // Blend weighted colors only near the two boundaries, retaining broad Toon bands.
            // Keep both symmetric windows above the grazing clamp and below one, and disjoint.
            // There mu/max(mu,.12)=1; symmetric smoothstep has the same integral as a step.
            // Thus the existing analytic diffuse normalization remains exact.
            float width=max(0.0f,min(0.045f,min(0.49f*(b.bands.y-b.bands.x),
                                                min(b.bands.x-0.12f,1.0f-b.bands.y))));
            if (width>1e-6f) {
                float shallow=smoothstep(b.bands.x-width,b.bands.x+width,nl);
                float lit=smoothstep(b.bands.y-width,b.bands.y+width,nl);
                float3 dark_color=b.bands.z*b.shadow_tint;
                float3 mid_color=b.bands.w*b.shallow_tint;
                band_response=dark_color+shallow*(mid_color-dark_color)+lit*(f3(1)-mid_color);
            }
        }
        if (b.face) {
            // SDF is an angular reflectance mask, evaluated again for each sampled
            // incoming direction. It never replaces incident radiance or visibility.
            float threshold=1-(dot(b.head_forward.xz(),l.xz())*0.5f+0.5f);
            float sdf=dot(b.head_right.xz(),l.xz())>0 ? b.face_sdf.y : b.face_sdf.x;
            float lit=smoothstep(threshold-0.5f,threshold+0.5f,sdf);
            q=lit<0.25f ? b.face_levels.x : lit<0.50f ? b.face_levels.y : 1.0f;
            tint=lit<0.25f ? b.shadow_tint : lit<0.50f ? b.shallow_tint : f3(1);
            band_response=q*tint;
            // Conservative bound: q<=1, tint<=1 => integral f*cos dOmega<=.94.
            // Face angular masks cannot use the fixed normal-band normalization.
            normalization=2.0f;
        }
        diffuse*=band_response/(normalization*max(nl,0.12f));
    }
    float3 spec_scale=b.toon ? b.spec_tint*b.spec_mask : f3(1);
    return (1-b.metallic)*(1-F)*diffuse+D*G*F*spec_scale;
}
PT_FN bool character_sample(const CharacterBSDF &b,const Hit &h,float3 v,GPURandom &rng,
                             float3 &direction,float3 &weight,float &pdf) {
    if (source_ink(b)) {
        direction=random_unit(rng);
        pdf=1/(4*PI);
        weight=2*source_toon_response(b,h.normal,v,direction); // f*abs(cos)/pdf.
        return all(isfinite(weight));
    }
    if (b.source) {
        // Uniform incident hemisphere: no invented anisotropic lobe or mixture parameter.
        direction=random_unit(rng);if (dot(direction,h.normal)<0) direction=-direction;
        if (dot(direction,h.geometric)<=0) return false;
        pdf=1/(2*PI);
        weight=source_toon_response(b,h.normal,v,direction); // f*cos/pdf cancels exactly.
        return all(isfinite(weight));
    }
    if (random_uniform(rng)<b.spec_probability) {
        float u=min(random_uniform(rng),0.999999f),phi=2*PI*random_uniform(rng);
        float a=b.roughness*b.roughness,ct=sqrt((1-u)/(1+(a*a-1)*u)),st=sqrt(max(0.0f,1-ct*ct));
        float3 axis=abs(h.normal.y)<0.95f ? f3(0,1,0) : f3(1,0,0);
        float3 tangent=normalize(cross(axis,h.normal)),bitangent=cross(h.normal,tangent);
        float3 half_vector;
        if (b.toon) {
            // Sample exactly the compact NDF used in character_eval; invalid reflected
            // directions terminate the sample instead of resampling a different PDF.
            // Hair's projected radial CDF is 2*s-s*s, with s=r*r.
            // Its inverse matches the tapered NDF in evaluation and therefore the MIS PDF.
            float radius=sqrt(b.hair ? u/(1+sqrt(1-u)) : u);
            float x=b.ax*radius*cos(phi),y=b.ay*radius*sin(phi);
            half_vector=b.tangent*x+cross(h.normal,b.tangent)*y+h.normal*sqrt(max(0.0f,1-x*x-y*y));
        } else half_vector=normalize(tangent*(st*cos(phi))+bitangent*(st*sin(phi))+h.normal*ct);
        direction=reflect(-v,half_vector);
    } else direction=normalize(h.normal+random_unit(rng));
    if (dot(direction,h.geometric)<=0) return false;
    float3 f=character_eval(b,h.normal,v,direction,pdf);
    if (pdf<=1e-8f) return false;
    weight=f*max(dot(h.normal,direction),0.0f)/pdf;
    return all(isfinite(weight));
}
// A finite sun is an actual distant emitter. Its integrated radiance equals
// the old delta-light strength; the surface cosine still controls irradiance.
// tone.w is the cone solid angle in an existing reserved slot (no ABI change).
PT_FN float sun_pdf(GPUParams &p,float3 l) {
    if (!p.extensions.z || p.toon_light.w<=0 || p.lighting.x<=0 || p.tone.w<=0) return 0;
    return dot(p.toon_light.xyz(),l)>=1-p.tone.w/(2*PI) ? 1/p.tone.w : 0;
}
PT_FN float3 sample_sun(GPUParams &p,GPURandom &rng) {
    if (p.tone.w<=0) return p.toon_light.xyz();
    float cosine=1-random_uniform(rng)*(p.tone.w/(2*PI));
    float sine=sqrt(max(0.0f,1-cosine*cosine)),phi=2*PI*random_uniform(rng);
    float3 axis=abs(p.toon_light.y)<.95f ? f3(0,1,0) : f3(1,0,0);
    float3 right=normalize(cross(axis,p.toon_light.xyz())),up=cross(p.toon_light.xyz(),right);
    return normalize(p.toon_light.xyz()*cosine+(right*cos(phi)+up*sin(phi))*sine);
}
PT_NOINLINE float3 character_direct(const Scene &s,const Hit &h,const CharacterBSDF &b,float3 v,
                               GPURandom &rng,GPUParams &p,bool continuation=true) {
    float3 result=f3(0);
    if (p.extensions.z && p.toon_light.w>0 && p.lighting.x>0) {
        float3 l=sample_sun(p,rng);float pdf;
        if (character_support(b,h,l)) {
            float visibility=mmd_shadow(s,h,l,10000);
            if (visibility>0) {
                float3 f=character_eval(b,h.normal,v,l,pdf);
                float light_pdf=p.tone.w>0 ? 1/p.tone.w : 0;
                float mis=continuation && light_pdf>0 ? power_heuristic(light_pdf,pdf) : 1;
                // Le/light_pdf = integrated radiance for the finite cone too.
                result+=f*p.lighting.x*character_cosine(b,h.normal,l)*visibility*mis;
            }
        }
        // Only the zero-angle emitter is delta; finite cones also use BSDF-hit MIS.
    }
    uint area_id;float3 area_l,area_Le;float area_distance,area_density;
    if (sample_area(p,h.p,rng,area_id,area_l,area_Le,area_distance,area_density) && character_support(b,h,area_l)) {
        float bsdf_pdf;float3 f=character_eval(b,h.normal,v,area_l,bsdf_pdf);
        result+=f*area_Le*(character_cosine(b,h.normal,area_l)/area_density)*
                mmd_shadow(s,h,area_l,area_distance,int(area_id))*
                (continuation ? power_heuristic(area_density,bsdf_pdf) : 1.0f);
    }
    if (p.lighting.w<=0) return result;
    float3 ln=random_unit(rng),point=LIGHT_CENTER+LIGHT_RADIUS*ln,delta=point-h.p;
    float distance=length(delta); if (distance<0.002f) return result;
    float3 l=delta/distance;float bsdf_pdf;
    // Far-side samples are hidden by the spherical emitter itself. Keep the
    // uniform-area PDF and count these as null samples, rather than resampling.
    if (p.extensions.z && dot(ln,-l)<=0) return result;
    float3 f=character_eval(b,h.normal,v,l,bsdf_pdf);
    float light_density=light_pdf(h.p,point);if (light_density<=0 || !character_support(b,h,l)) return result;
    return result+f*LIGHT_EMISSION*p.lighting.w*(character_cosine(b,h.normal,l)/light_density)*
           mmd_shadow(s,h,l,distance)*(continuation ? power_heuristic(light_density,bsdf_pdf) : 1.0f);
}

PT_FN float3 trace_path(const Scene &s, Ray ray, uint depth, GPURandom &rng,
                         GPUParams &p, float &primary_distance,
                         float initial_pdf=0, float3 initial_point=f3(0))
{
    primary_distance=10000.0f;
    float3 radiance=f3(0), beta=f3(1), previous_point=initial_point;
    float previous_pdf = initial_pdf;
    bool previous_mis = initial_pdf>0;
    for (uint bounce = 0; bounce < depth; ++bounce) {
        Hit rec;
        bool found=p.settings.z ? mmd_visible_hit(s,ray,rng,rec)
                                : world_hit(s, ray, 0.001f, 10000.0f, rec);
        if (!found) {
            float t = 0.5f*(normalize(ray.direction).y+1);
            float3 sky=(p.rendering.w&2u) ? f3(1) : ((1-t)*f3(1)+t*f3(0.5f,0.7f,1));
            radiance += beta*p.lighting.y*sky;
            float cone_pdf=sun_pdf(p,normalize(ray.direction));
            if (cone_pdf>0) {
                float mis=previous_mis ? power_heuristic(previous_pdf,cone_pdf) : 1;
                radiance+=beta*(p.lighting.x/p.tone.w)*mis;
            }
            break;
        }
        if (bounce==0) primary_distance=rec.t;
        const GPUMaterial &m = s.materials[rec.material];
        if (m.flags.x==4) {
            apply_normal_map(s,rec,p);
            CharacterBSDF bsdf=character_bsdf(s,rec,p);
            if (bsdf.source) bsdf.source_view_origin=ray.origin;
            radiance+=beta*source_emission(bsdf); // No NEE sampler for these texels: BSDF hits use weight 1.
            float3 view=-normalize(ray.direction);
            radiance+=beta*character_direct(s,rec,bsdf,view,rng,p,bounce+1<depth);
            if (bounce==depth-1) break;
            float3 direction,weight;float pdf;
            if (!character_sample(bsdf,rec,view,rng,direction,weight,pdf)) break;
            beta*=weight;
            previous_mis=true;previous_point=rec.p;previous_pdf=pdf;
            // Unbiased roulette bounds long indirect paths; survivor weight compensates.
            if (bounce>=3) {
                float survive=clamp(max(beta.x,max(beta.y,beta.z)),0.05f,0.95f);
                if (random_uniform(rng)>survive) break;
                beta/=survive;
            }
            ray=Ray{offset_mmd_ray(rec,direction),direction};
            continue;
        }
        const bool area_emitter=m.flags.x==3 && m.flags.w>0;
        float3 emission=m.flags.x==3 && ((area_emitter || p.extensions.z) ? rec.front_face : true)
            ? m.color.xyz()*(area_emitter ? 1.0f : p.lighting.w) : f3(0);
        float emitter_pdf=area_emitter ? area_pdf(p,m.flags.w-1,previous_point,rec.p) : light_pdf(previous_point,rec.p);
        float weight=previous_mis && any(emission>0) ? power_heuristic(previous_pdf,emitter_pdf) : 1;
        radiance += beta*emission*weight;
        float3 attenuation=f3(1), direction;
        bool diffuse = m.flags.x == 0;
        if (diffuse) {
            attenuation = albedo_at(s, rec);
            radiance+=beta*diffuse_area_direct(s,rec,attenuation,rng,p,bounce+1<depth);
            radiance += beta*p.lighting.w*direct_light(s, rec, attenuation, rng,p.settings.z!=0,bounce+1<depth);
            if (p.extensions.z && p.toon_light.w>0 && p.lighting.x>0) {
                float3 sun=sample_sun(p,rng);
                float cosine=max(dot(rec.normal,sun),0.0f);
                float mis=p.tone.w>0 && bounce+1<depth ? power_heuristic(1/p.tone.w,cosine/PI) : 1;
                if (dot(rec.geometric,sun)>0)
                    radiance+=beta*attenuation/PI*p.lighting.x*cosine*mis*mmd_shadow(s,rec,sun,10000);
            }
            direction = rec.normal+random_unit(rng);
            if (dot(direction, direction) <= 1e-12f) direction = rec.normal;
        } else if (m.flags.x == 1) {
            // Match the current CUDA metal material, including its unnormalized incident direction.
            direction = ray.direction-2*dot(ray.direction, rec.normal)*rec.normal
                +m.color.w*random_ball(rng);
            if (dot(normalize(direction), rec.normal) <= 0) break;
            attenuation = m.color.xyz();
        } else if (m.flags.x == 2) {
            float3 incoming = normalize(ray.direction);
            bool inside = dot(incoming, rec.normal) > 0;
            float3 normal = inside ? -rec.normal : rec.normal;
            float ratio = inside ? m.color.w : 1.0f/m.color.w;
            float dt = dot(incoming, normal);
            float discriminant = 1-ratio*ratio*(1-dt*dt);
            float3 reflected = incoming-2*dot(incoming, normal)*normal;
            if (discriminant < 0) direction = reflected;
            else {
                float3 refracted = ratio*(incoming-normal*dt)-normal*sqrt(discriminant);
                direction = random_uniform(rng) < schlick_probability(-dt, m.color.w)
                    ? reflected : refracted;
            }
        } else break;
        if (bounce == depth-1) break;
        previous_mis = diffuse; previous_point = rec.p;
        previous_pdf = diffuse ? max(dot(rec.normal, normalize(direction)), 0.0f)/PI : 0;
        beta *= attenuation;
        ray = Ray{p.settings.z ? offset_mmd_ray(rec,direction) : rec.p, direction};
    }
    return radiance;
}


PT_FN float3 character_path_color(const Scene &s,const Hit &h,Ray incoming,
                                  uint depth,GPURandom &rng,GPUParams &p,float3 base) {
    float3 direct_toon=shade_mmd(s,h,p,base);
    float strength=as_type<float>(p.extensions.y);
    if (depth<=1 || strength<=0 || (p.features.w&(64u|128u|16384u|32768u|65536u|131072u))) return direct_toon;
    CharacterBSDF bsdf=character_bsdf(s,h,p);
    float3 direction,weight;float pdf;
    float3 direct=character_direct(s,h,bsdf,-normalize(incoming.direction),rng,p);
    if (!character_sample(bsdf,h,-normalize(incoming.direction),rng,direction,weight,pdf)) return direct_toon+strength*direct;
    float unused;
    float3 incident=trace_path(s,Ray{offset_mmd_ray(h,direction),direction},depth-1,rng,p,unused,pdf,h.p);
    return direct_toon+strength*(direct+weight*(incident-sky_color(direction)));
}

PT_FN float4 zzz_eye_reveal(const Scene &s,Ray ray,const Hit &front,GPUParams &p) {
    const GPUMaterial &hair=s.materials[front.material];
    if (p.features.w&1024u || hair.mmd.y<=0 || hair.zzz_face_detail.w<=0 || uint(hair.zzz_misc.y)!=4) return f4(0);
    if (dot(normalize(p.origin.xyz()-front.p),hair.zzz_head_forward.xyz())<=0.1f) return f4(0);
    Hit eye;
    if (!world_hit(s,ray,front.t+0.00001f,10000,eye,5)) return f4(0);
    if ((eye.t-front.t)*length(ray.direction)>hair.zzz_alpha.z) return f4(0);
    float reveal=1,minimum=0.00001f;Hit blocker;bool clear=false;
    for (uint i=0;i<32;++i) {
        if (!world_hit(s,ray,minimum,eye.t-0.00002f,blocker)) {clear=true;break;}
        const GPUMaterial &m=s.materials[blocker.material];
        if (m.zzz_misc.w<=0 || uint(m.zzz_misc.y)!=4 || !m.roughness_image.w) return f4(0);
        float mask=mmd_texture(s,m.roughness_image,surface_uv(s,blocker),false,true).r;
        reveal*=1-max(clamp(mask,0.0f,1.0f),m.zzz_alpha.y);
        if (reveal<=0.0001f) return f4(0);
        minimum=blocker.t+0.00001f;
    }
    if (!clear) return f4(0); // Never reveal through an unexamined occluder.
    float3 color=shade_mmd(s,eye,p,mmd_base(s,eye).xyz());
    return f4(color,clamp(reveal,0.0f,1.0f));
}
PT_FN float3 trace_character(const Scene &s,Ray ray,uint depth,GPURandom &rng,
                               GPUParams &p,float &primary_distance) {
    float3 color=f3(0);float transmittance=1,minimum=0.00001f;primary_distance=10000;
    for (uint layer=0;layer<64;++layer) {
        Hit hit;
        if (!world_hit(s,ray,minimum,10000,hit)) return color+transmittance*sky_color(ray.direction);
        const GPUMaterial &m=s.materials[hit.material];
        if (m.flags.x!=4) {
            float unused;
            Ray background={ray.origin+(minimum-0.00001f)*ray.direction,ray.direction};
            return color+transmittance*trace_path(s,background,depth,rng,p,unused);
        }
        float4 base=mmd_base(s,hit);float alpha=clamp(base.a,0.0f,1.0f);
        if (alpha>0) {
            if (primary_distance==10000) primary_distance=hit.t;
            apply_normal_map(s,hit,p);
            float3 shaded=character_path_color(s,hit,ray,depth,rng,p,base.xyz());
            if (layer==0) {
                float4 eye=zzz_eye_reveal(s,ray,hit,p);
                shaded=mix(shaded,eye.xyz(),eye.a);
                if (p.features.w&131072u) shaded=f3(eye.a);
            }
            color+=transmittance*alpha*shaded;transmittance*=1-alpha;
            if (transmittance<0.0001f) return color;
        }
        minimum=hit.t+0.00001f/max(length(ray.direction),0.00001f);
    }
    // Finite layer bound: remaining energy sees the sky, never uninitialised data.
    return color+transmittance*sky_color(ray.direction);
}
// Stable depth prepass uses an opacity threshold; shading still blends alpha.
PT_FN void depth_kernel(const GPUPrimitive *primitives ,
    const GPUNode *nodes ,const GPUMaterial *materials ,
    const uchar *pixels ,GPUParams &p ,
    float *depths ,uint2 xy ) {
    if (xy.x>=p.image.x || xy.y>=p.image.y) return;
    Scene s{primitives,nodes,materials,pixels,&p,nullptr};
    float2 uv=(f2(xy)+0.5f)/f2(p.image.xy());
    Ray ray={p.origin.xyz(),normalize(p.lower_left.xyz()+uv.x*p.horizontal.xyz()+uv.y*p.vertical.xyz()-p.origin.xyz())};
    float depth=10000,minimum=0.00001f;Hit h;
    for (uint i=0;i<64;++i) {
        if (!world_hit(s,ray,minimum,10000,h)) break;
        if (materials[h.material].flags.x!=4 || mmd_base(s,h).a>=0.5f) {
            depth=-dot(h.p-p.origin.xyz(),normalize(cross(p.right.xyz(),p.up.xyz())));break;
        }
        minimum=h.t+0.00001f;
    }
    depths[xy.y*p.image.x+xy.x]=depth;
}

PT_FN float3 hair_overlay(const Scene &s, Ray ray, GPUParams &p,
                           float visible_distance)
{
    Hit shell;
    // Limit to the nearest visible regular surface: an occluded shell must not
    // shine through the face, accessories or the other side of the head.
    float tolerance=0.0002f/max(length(ray.direction),0.00001f);
    // Reuse THIS sample's primary visibility, including its alpha decision.
    // No extra random draws: toggling the overlay leaves all base samples unchanged.
    if (!world_hit(s,ray,0.00001f,visible_distance+tolerance,shell,1)) return f3(0);
    float4 base=mmd_base(s,shell);
    if (base.w<=0) return f3(0);
    return base.w*shade_mmd(s,shell,p,base.xyz());
}
PT_FN void render_kernel(
    const GPUPrimitive *primitives ,
    const GPUNode *nodes ,
    const GPUMaterial *materials ,
    const uchar *pixels ,
    GPURandom *states ,
    GPUParams &p ,
    float4 *frame ,
    float4 *accum ,
    const float *depths ,
    uint2 xy )
{
    xy += u2(as_type<uint>(p.tone.z),as_type<uint>(p.tone.y)); // Global pixel of the submitted tile.
    if (xy.x >= p.image.x || xy.y >= p.image.y) return;
    uint i = xy.y*p.image.x+xy.x;
    GPURandom rng = p.counts.w == 0 ? seed_random(1984+i) : states[i];
    Scene scene{primitives, nodes, materials, pixels,&p,depths};
    uint n = uint(sqrt(float(p.image.z)));
    float3 color=f3(0);
    for (uint si = 0; si < n; ++si) for (uint sj = 0; sj < n; ++sj) {
        float u = (xy.x+(si+random_uniform(rng))/n)/p.image.x;
        float v = (xy.y+(sj+random_uniform(rng))/n)/p.image.y;
        float2 disk = p.origin.w*random_disk(rng);
        float3 offset = p.right.xyz()*disk.x+p.up.xyz()*disk.y;
        Ray ray{p.origin.xyz()+offset,
                p.lower_left.xyz()+u*p.horizontal.xyz()+v*p.vertical.xyz()-p.origin.xyz()-offset};
        float primary_distance;
        color += trace_path(scene, ray, p.image.w, rng,p,primary_distance);
        // Pure packages forbid the legacy hair overlay.
    }
    frame[i] = f4(color/float(p.image.z), 0);
    if (p.settings.x) accum[i] += frame[i];
    states[i] = rng;
}
PT_FN void bilateral_kernel(
    const float4 *input ,
    float4 *output ,
    GPUParams &p ,
    uint2 xy )
{
    if (xy.x >= p.image.x || xy.y >= p.image.y) return;
    uint i = xy.y*p.image.x+xy.x;
    int radius = int(ceil(2*p.filter.x));
    float3 sum=f3(0); float total = 0;
    float sigma_r = p.filter.y*(p.settings.x ? float(p.counts.z) : 1.0f);
    for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
        int gx = int(xy.x)+x, gy = int(xy.y)+y;
        if (gx < 0 || gy < 0 || gx >= int(p.image.x) || gy >= int(p.image.y)) continue;
        float3 c = input[uint(gy)*p.image.x+uint(gx)].xyz();
        float3 difference = input[i].xyz()-c;
        float weight = exp(-float(x*x+y*y)/(2*p.filter.x*p.filter.x))
            *exp(-dot(difference, difference)/(2*sigma_r*sigma_r));
        sum += weight*c; total += weight;
    }
    output[i] = f4(sum/total, 0);
}
// Stable four-sample geometry outline pass, independent of path RNG.
// Like the raster pass, front faces are culled and the hull is depth-tested
// against visible unexpanded geometry. Alpha clipping is added for MMD sleeves.
PT_FN void guide_kernel(
    const GPUPrimitive *primitives ,
    const GPUNode *nodes ,
    const GPUMaterial *materials ,
    const uchar *pixels ,
    GPUParams &p ,
    float4 *guides ,
    uint *guide_materials ,
    uint2 xy )
{
    if (xy.x>=p.image.x || xy.y>=p.image.y) return;
    uint index=xy.y*p.image.x+xy.x;
    Scene scene{primitives,nodes,materials,pixels,&p,nullptr};
    float3 sum=f3(0);float coverage=0;
    for (uint sample=0;sample<4;++sample) {
        float2 sub=f2((sample&1)?0.75f:0.25f,(sample&2)?0.75f:0.25f);
        float u=(xy.x+sub.x)/p.image.x,v=(xy.y+sub.y)/p.image.y;
        Ray ray{p.origin.xyz(),normalize(p.lower_left.xyz()+u*p.horizontal.xyz()+v*p.vertical.xyz()-p.origin.xyz())};
        Hit surface;float visible=10000,minimum=0.00001f;
        for (uint layer=0;layer<64;++layer) {
            if (!world_hit(scene,ray,minimum,10000,surface)) break;
            if (materials[surface.material].flags.x!=4 || mmd_base(scene,surface).w>=0.5f) {
                visible=surface.t;break;
            }
            minimum=surface.t+0.00001f;
        }
        Hit hull;minimum=0.00001f;
        for (uint layer=0;layer<64;++layer) {
            if (!world_hit(scene,ray,minimum,visible,hull,3)) break;
            float4 diffuse=mmd_base(scene,hull);
            if (diffuse.w>=0.5f) {
                const GPUMaterial &m=materials[hull.material];
                // zzz-common.hlsl outline_color: material tint * diffuse,
                // pow(tint*.5,1.5), and a light-dependent dark/bright mix.
                // PMX material tints replace HoyoToon's unavailable material-ID LUT.
                float3 outline_tint=m.edge_color.xyz();
                if (m.zzz_misc.w>0) {
                    float id=mmd_texture(scene,m.metallic_image,surface_uv(scene,hull),false,true).x;
                    outline_tint=m.zzz_outline[zzz_region_id(id)].xyz();
                }
                float3 tint=clamp(pow(max(outline_tint,f3(0))*0.5f,f3(1.5f)),0.0f,1.0f);
                float3 normal=-hull.normal; // primitive_hit faces its normal toward the ray.
                float red=clamp(dot(normal,zzz_light(hull.p,p)),0.0f,1.0f)*0.5f+0.5f;
                float alpha=clamp(m.edge_color.w,0.0f,1.0f);
                sum+=diffuse.xyz()*tint*mix(0.01f,1.0f,red)*alpha*0.25f;
                coverage+=alpha*0.25f;break;
            }
            minimum=hull.t+0.00001f;
        }
    }
    guides[index]=f4(sum,coverage);
    guide_materials[index]=0xffffffffu; // Retained buffer binding for ABI compatibility.
}
// Compress the highest channel with a C1-continuous shoulder. Multiplying all
// channels by the same ratio preserves hue and keeps sub-knee colors unchanged.
PT_FN float3 tone_map(float3 color,float knee) {
    color=max(color,f3(0));float peak=max(color.x,max(color.y,color.z));
    if (peak<=knee) return color;
    float mapped=knee+(1-knee)*(1-exp(-(peak-knee)/(1-knee)));
    return color*(mapped/peak);
}
PT_FN uchar display_byte(float x)
{
    x = clamp(x, 0.0f, 1.0f);
    float c = x <= 0.0031308f ? 12.92f*x : 1.055f*pow(x, 1.0f/2.4f)-0.055f;
    return uchar(255*c+0.5f);
}
PT_FN void display_kernel(
    const float4 *input ,
    uchar4 *output ,
    GPUParams &p ,
    const float4 *guides ,
    const GPUMaterial *materials ,
    const uint *guide_materials ,
    uint2 xy )
{
    if (xy.x >= p.image.x || xy.y >= p.image.y) return;
    float3 c = input[xy.y*p.image.x+xy.x].xyz()*p.filter.z*p.lighting.z;
    uint index=xy.y*p.image.x+xy.x;
    if (p.settings.z && (p.features.x&1u)) {
        float4 outline=guides[index];
        c=c*(1-outline.w)+outline.xyz();
    }
    if (p.rendering.y) c=tone_map(c,p.tone.x);
    // Same bottom-up framebuffer -> top-down BGRA conversion as CUDA/GDI.
    output[(p.image.y-1-xy.y)*p.image.x+xy.x] =
        c4(display_byte(c.z), display_byte(c.y), display_byte(c.x), 255);
}

} // namespace pt
