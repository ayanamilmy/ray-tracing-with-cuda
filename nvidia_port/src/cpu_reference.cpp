// CPU validation executes the SAME translated pixel routines as the CUDA backend.
#include "../include/pathtracer.h"
#include "../include/scene_io.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <exception>
int main(int argc,char **argv) {try {
 if(argc<4 || argc>6) {std::cerr<<"Usage: cpu_reference scene.npt image.ppm passes [threads] [linear.f32]\n";return 2;}
 auto s=pt::load_scene(argv[1]);uint passes=pt::positive_arg(argv[3]);
 uint threads=argc>4 ? pt::positive_arg(argv[4]) : std::max(1u,std::thread::hardware_concurrency());
 threads=std::min(threads,s.params.image.y);size_t count=size_t(s.params.image.x)*s.params.image.y;
 std::vector<pt::GPURandom> rng(count);std::vector<pt::float4> frame(count),accum(count);std::vector<pt::uchar4> image(count);
 auto start=std::chrono::steady_clock::now();
 for(uint pass=0;pass<passes;++pass) {
  auto p=s.params;p.counts.z=pass+1;p.counts.w=pass;p.filter.z=1.f/(pass+1);
  std::atomic<uint> row{0};std::vector<std::thread> workers;
  for(uint t=0;t<threads;++t) workers.emplace_back([&] {
   for(uint y;(y=row.fetch_add(1))<p.image.y;) for(uint x=0;x<p.image.x;++x) {
    auto xy=pt::u2(x,y);
    pt::render_kernel(s.primitives.data(),s.nodes.data(),s.materials.data(),s.pixels.data(),rng.data(),p,frame.data(),accum.data(),nullptr,xy);
   }
  });
  for(auto &t:workers) t.join();std::cout<<"pass="<<pass+1<<"/"<<passes<<std::endl;
 }
 auto p=s.params;p.filter.z=1.f/passes;
 for(uint y=0;y<p.image.y;++y) for(uint x=0;x<p.image.x;++x)
  pt::display_kernel(accum.data(),image.data(),p,nullptr,s.materials.data(),nullptr,pt::u2(x,y));
 pt::write_ppm(argv[2],image,p.image.x,p.image.y);
 if(argc>5) pt::write_linear(argv[5],accum,1.f/passes);
 double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 std::cout<<"CPU reference: "<<p.image.x<<"x"<<p.image.y<<", "<<uint64_t(p.image.z)*passes<<" spp, "<<seconds<<" seconds\n";
 return 0;
 }catch(const std::exception &e) {std::cerr<<e.what()<<"\n";return 1;}}
