#pragma once
#include "types.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <limits>
#include <algorithm>
namespace pt {
struct PackedScene {
 GPUParams params{};
 std::vector<GPUPrimitive> primitives;
 std::vector<GPUNode> nodes;
 std::vector<GPUMaterial> materials;
 std::vector<uchar> pixels;
};
template<class T> inline void read_array(std::ifstream &in,std::vector<T> &out,uint n) {
 out.resize(n);in.read(reinterpret_cast<char*>(out.data()),std::streamsize(out.size()*sizeof(T)));
 if(!in) throw std::runtime_error("Truncated scene package");
}
inline PackedScene load_scene(const char *path) {
 if(__BYTE_ORDER__!=__ORDER_LITTLE_ENDIAN__) throw std::runtime_error("Little-endian host required");
 std::ifstream in(path,std::ios::binary);if(!in) throw std::runtime_error("Cannot open scene package");
 char magic[8];uint sizes[4];PackedScene s;
 in.read(magic,8);in.read(reinterpret_cast<char*>(sizes),sizeof(sizes));
 if(!in || std::memcmp(magic,"NPTSCN01",8)) throw std::runtime_error("Unsupported scene format");
 uint64_t length=24+sizeof(GPUParams)+uint64_t(sizes[0])*sizeof(GPUPrimitive)+uint64_t(sizes[1])*sizeof(GPUNode)+uint64_t(sizes[2])*sizeof(GPUMaterial)+sizes[3];
 if(length!=std::filesystem::file_size(path) || !sizes[0] || !sizes[1] || !sizes[2] || sizes[3]<4) throw std::runtime_error("Invalid scene lengths");
 in.read(reinterpret_cast<char*>(&s.params),sizeof(s.params));
 read_array(in,s.primitives,sizes[0]);read_array(in,s.nodes,sizes[1]);read_array(in,s.materials,sizes[2]);read_array(in,s.pixels,sizes[3]);
 auto &p=s.params;
 uint n=uint(std::sqrt(double(p.image.z)));
 if(!p.image.x || !p.image.y || uint64_t(p.image.x)*p.image.y>UINT32_MAX || !p.image.z || n*n!=p.image.z || !p.image.w ||
    p.counts.x!=sizes[0] || p.counts.y!=sizes[1] || p.extensions.z!=2 || !(p.rendering.w&1) || !p.settings.x || !p.settings.z || !p.settings.w || (p.features.x&1) || p.settings.y || p.rendering.x>4)
  throw std::runtime_error("Scene must use pure source-material path transport, square samples, no overlays/filter");
 if(p.features.x && (p.features.x&6)!=6) throw std::runtime_error("Only baked path-outline geometry is supported");
 for(const auto &g:s.primitives) if(g.meta.y>=s.materials.size() || g.meta.x>2) throw std::runtime_error("Invalid primitive");
 for(uint i=0;i<s.nodes.size();++i) {const auto &link=s.nodes[i].link;
  if(link.w ? link.z>=s.primitives.size() : link.x<=i || link.y<=i || link.x>=s.nodes.size() || link.y>=s.nodes.size()) throw std::runtime_error("Invalid or cyclic BVH");
 }
 auto texture=[&](U4 image) {if(!image.w) return;uint64_t bytes=uint64_t(image.y)*image.z*4;
  if(!image.y || !image.z || uint64_t(image.x)+bytes>s.pixels.size()) throw std::runtime_error("Texture range outside package");};
 for(const auto &m:s.materials) {
  texture(m.image);texture(m.toon_image);texture(m.sphere_image);texture(m.normal_image);texture(m.metallic_image);texture(m.roughness_image);texture(m.zzz_face_image);texture(m.zzz_eye_lut);
  for(const auto &im:m.zzz_matcap_images) texture(im);
  if(m.surface_flags.x>3 || m.surface_flags.y>3) throw std::runtime_error("Invalid material channel");
 }
 p.tone.y=p.tone.z=0;p.counts.z=1;p.counts.w=0;p.filter.z=1;
 return s;
}
inline void write_ppm(const char *path,const std::vector<uchar4> &bgra,uint w,uint h) {
 std::ofstream out(path,std::ios::binary);if(!out) throw std::runtime_error("Cannot create PPM");
 out<<"P6\n"<<w<<" "<<h<<"\n255\n";
 for(auto a:bgra) {char rgb[]={char(a.z),char(a.y),char(a.x)};out.write(rgb,3);}out.close();if(!out) throw std::runtime_error("PPM write failed");
}
inline void write_linear(const char *path,const std::vector<float4> &linear,float inv) {
 std::ofstream out(path,std::ios::binary);if(!out) throw std::runtime_error("Cannot create linear dump");
 for(auto a:linear) {a*=inv;out.write(reinterpret_cast<const char*>(&a),sizeof(a));}
 if(!out) throw std::runtime_error("Linear dump write failed");
}
inline uint positive_arg(const char *text) {
 std::string arg(text);size_t end=0;auto n=std::stoull(arg,&end);if(end!=arg.size() || n==0 || n>UINT32_MAX || arg[0]=='-') throw std::runtime_error("Expected positive integer");return uint(n);
}
} // namespace pt
