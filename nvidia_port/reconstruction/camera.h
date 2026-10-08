#pragma once
#include "reconstruction.h"
#include "launch.h"
#include <algorithm>
namespace reconstruction {
inline float halton(unsigned n,unsigned base) {
    float value=0,scale=1;for(;n;n/=base){scale/=base;value+=scale*(n%base);}return value;
}
inline void camera(Frame &f,const pt::GPUParams &p) {
    using namespace pt;
    auto r=p.right.xyz,u=p.up.xyz,b=normalize(cross(r,u)),o=p.origin.xyz;
    float view[]={r.x,r.y,r.z,-dot(r,o),u.x,u.y,u.z,-dot(u,o),b.x,b.y,b.z,-dot(b,o),0,0,0,1};
    std::copy(view,view+16,f.world_to_view);
    float focus=-dot(p.lower_left.xyz+.5f*p.horizontal.xyz+.5f*p.vertical.xyz-o,b);
    float near=.01f,far=10000;
    float projection[]={2*focus/length(p.horizontal.xyz),0,0,0,0,2*focus/length(p.vertical.xyz),0,0,
        0,0,-(far+near)/(far-near),-2*far*near/(far-near),0,0,-1,0};
    std::copy(projection,projection+16,f.view_to_clip);
}
}
