// CPU checks for real packed asset geometry and RT hit-attribute reconstruction.
#include "../build/optix/pathtracer.h"
#include "geometry.h"
#include "hit_rules.h"
#include <iostream>
int main(int argc,char **argv) {try {
    if(argc!=2) return 2;
    auto s=pt::load_scene(argv[1]);auto geometry=rt::geometry(s);unsigned checked=0,mismatch=0;
    for(unsigned id:geometry.triangles) {
        const auto &g=s.primitives[id];
        auto cross=pt::cross(g.b.xyz-g.a.xyz,g.c.xyz-g.a.xyz);
        if(pt::dot(cross,cross)<1e-16f) continue;
        auto n=pt::normalize(cross),point=0.25f*g.a.xyz+0.35f*g.b.xyz+0.4f*g.c.xyz;
        for(float sign:{-1.f,1.f}) {
            pt::Ray ray{point+sign*0.01f*n,-sign*n};pt::Hit hit{};
            if(!pt::primitive_hit(g,ray,0.00001f,1.f,hit)) continue;
            auto rt_hit=rt::triangle_hit(g,ray,hit.t,hit.bary.y,hit.bary.z,id);
            if(pt::length(hit.normal-rt_hit.normal)>1e-5f || pt::length(hit.uv-rt_hit.uv)>1e-5f ||
               hit.front_face!=rt_hit.front_face || hit.has_uv!=rt_hit.has_uv || hit.material!=rt_hit.material) ++mismatch;
            ++checked;
        }
    }
    auto broken=s;broken.primitives[geometry.triangles.front()].meta.x=2;
    broken.primitives[geometry.triangles.front()].meta.w=0xffffff00;
    bool rejected=false;try {rt::geometry(broken);} catch(const std::runtime_error&) {rejected=true;}
    if(!rejected) throw std::runtime_error("Malformed hull source index was accepted");
    std::cout<<"{\"triangle_count\":"<<geometry.triangles.size()<<",\"sphere_count\":"<<geometry.spheres.size()
             <<",\"reconstructed_hits_checked\":"<<checked<<",\"mismatches\":"<<mismatch
             <<",\"invalid_hull_rejected\":true}\n";
    return mismatch || !checked ? 1:0;
}catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}}
