#pragma once
// Host-only transport. The scene, sample sequence and transport kernel are unchanged.
#include "../include/scene_io.h"
#include <istream>
#include <ostream>
#include <string>

namespace rt {
inline bool read_frame(std::istream &in, pt::GPUParams &next,
                       std::vector<pt::GPUPrimitive> &primitives,
                       std::vector<pt::GPUMaterial> &materials,
                       const pt::GPUParams &current) {
    char magic[8];unsigned sizes[2];
    in.read(magic,sizeof(magic));
    if(in.gcount()==0 && in.eof()) return false;
    if(!in || std::memcmp(magic,"NPTFRM01",8)) throw std::runtime_error("Invalid frame magic");
    in.read(reinterpret_cast<char*>(sizes),sizeof(sizes));
    in.read(reinterpret_cast<char*>(&next),sizeof(next));
    if(!in || sizes[0]!=primitives.size() || sizes[1]!=materials.size() ||
       std::memcmp(&next.image,&current.image,sizeof(next.image)) ||
       next.extensions.z!=2 || !(next.rendering.w&1) || !next.settings.x ||
       !next.settings.z || !next.settings.w || (next.features.x&1) || next.settings.y)
        throw std::runtime_error("Invalid frame packet or changed render specification");
    in.read(reinterpret_cast<char*>(primitives.data()),std::streamsize(primitives.size()*sizeof(primitives[0])));
    in.read(reinterpret_cast<char*>(materials.data()),std::streamsize(materials.size()*sizeof(materials[0])));
    if(!in) throw std::runtime_error("Truncated frame packet");
    return true;
}

inline void write_image(std::ostream &out,const pt::uchar4 *image,unsigned width,
                        unsigned height,const std::string &report) {
    if(report.size()>1048576) throw std::runtime_error("Image report too large");
    unsigned header[]={width,height,unsigned(report.size())};
    out.write("NPTIMG01",8);
    out.write(reinterpret_cast<const char*>(header),sizeof(header));
    out.write(reinterpret_cast<const char*>(image),std::streamsize(uint64_t(width)*height*sizeof(*image)));
    out.write(report.data(),std::streamsize(report.size()));
    out.flush();
    if(!out) throw std::runtime_error("Image pipe write failed");
}
} // namespace rt
