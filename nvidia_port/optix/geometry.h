#pragma once
#include "../include/scene_io.h"
#include <array>
namespace rt {
// Explicit 12-byte vertices: Clang float3 is 16 bytes and cannot use the default stride.
struct Vertex { float x,y,z; };
struct Bounds { float minX,minY,minZ,maxX,maxY,maxZ; };
static_assert(sizeof(Vertex)==12 && sizeof(Bounds)==24);
struct Geometry {
    std::vector<Vertex> vertices;
    std::vector<unsigned> triangles,spheres;
    std::vector<Bounds> bounds;
};
inline Geometry geometry(const pt::PackedScene &s) {
    Geometry result;
    for(unsigned i=0;i<s.primitives.size();++i) {
        const auto &g=s.primitives[i];
        auto finite=[](pt::float4 a) {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);};
        if(!finite(g.a) || (g.meta.x && (!finite(g.b)||!finite(g.c)))) throw std::runtime_error("Non-finite geometry");
        if(g.meta.x==2 && ((g.meta.w>>8)>=s.primitives.size() || s.primitives[g.meta.w>>8].meta.x!=1))
            throw std::runtime_error("Invalid baked outline source index");
        if(!g.meta.x) {
            if(!std::isfinite(g.a.w)||g.a.w<=0) throw std::runtime_error("Invalid sphere radius");
            result.spheres.push_back(i);float r=g.a.w;
            result.bounds.push_back({g.a.x-r,g.a.y-r,g.a.z-r,g.a.x+r,g.a.y+r,g.a.z+r});
        } else {
            result.triangles.push_back(i);
            for(auto v:{g.a,g.b,g.c}) result.vertices.push_back({v.x,v.y,v.z});
        }
    }
    return result;
}
}
