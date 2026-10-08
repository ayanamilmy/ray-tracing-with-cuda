// Standalone NVIDIA backend. Intentionally does not include original CUDA/shared headers.
#include <cuda_runtime.h>
#include "../include/pathtracer.h"
#include "../include/scene_io.h"
#include <iostream>
#include <chrono>
#include <memory>
static void check(cudaError_t status,const char *operation) {
 if(status!=cudaSuccess) throw std::runtime_error(std::string(operation)+": "+cudaGetErrorString(status));
}
template<class T> struct Buffer {
 T *data=nullptr;size_t count=0;
 explicit Buffer(size_t n):count(n) { check(cudaMalloc(reinterpret_cast<void**>(&data),n*sizeof(T)),"cudaMalloc"); }
 explicit Buffer(const std::vector<T> &host):Buffer(host.size()) {check(cudaMemcpy(data,host.data(),count*sizeof(T),cudaMemcpyHostToDevice),"upload");}
 Buffer(const Buffer&)=delete;Buffer& operator=(const Buffer&)=delete;
 ~Buffer() {if(data) cudaFree(data);}
};
__global__ void trace_pixels(const pt::GPUPrimitive *g,const pt::GPUNode *b,const pt::GPUMaterial *m,const pt::uchar *pixels,pt::GPURandom *rng,
                            pt::GPUParams p,pt::float4 *frame,pt::float4 *accum,uint base,uint length) {
 uint i=base+blockIdx.x*blockDim.x+threadIdx.x;
 if(i-base>=length) return;
 pt::render_kernel(g,b,m,pixels,rng,p,frame,accum,nullptr,pt::u2(i%p.image.x,i/p.image.x));
}
__global__ void encode_pixels(const pt::float4 *accum,pt::uchar4 *out,pt::GPUParams p,const pt::GPUMaterial *m,uint base,uint length) {
 uint i=base+blockIdx.x*blockDim.x+threadIdx.x;
 if(i-base>=length) return;
 pt::display_kernel(accum,out,p,nullptr,m,nullptr,pt::u2(i%p.image.x,i/p.image.x));
}
int main(int argc,char **argv) {try {
 if(argc<4 || argc>6) {std::cerr<<"Usage: nvidia_render scene.npt image.ppm passes [tile_pixels] [linear.f32]\n";return 2;}
 auto s=pt::load_scene(argv[1]);uint passes=pt::positive_arg(argv[3]);uint tile=argc>4 ? pt::positive_arg(argv[4]) : 32768u;
 check(cudaSetDevice(0),"select GPU");
 check(cudaDeviceSetLimit(cudaLimitStackSize,16384),"path-tracer stack limit");cudaDeviceProp prop;check(cudaGetDeviceProperties(&prop,0),"GPU properties");
 std::cout<<"NVIDIA device: "<<prop.name<<std::endl;
 size_t n=size_t(s.params.image.x)*s.params.image.y;tile=uint(std::min<size_t>(tile,n));
 Buffer<pt::GPUPrimitive> g(s.primitives);Buffer<pt::GPUNode> b(s.nodes);Buffer<pt::GPUMaterial> m(s.materials);Buffer<pt::uchar> pixels(s.pixels);
 Buffer<pt::GPURandom> rng(n);Buffer<pt::float4> frame(n),accum(n);Buffer<pt::uchar4> bgra(n);
 check(cudaMemset(accum.data,0,n*sizeof(pt::float4)),"clear accumulation");
 auto start=std::chrono::steady_clock::now();
 for(uint pass=0;pass<passes;++pass) {
  auto p=s.params;p.counts.z=pass+1;p.counts.w=pass;p.filter.z=1.f/(pass+1);
  for(size_t base=0;base<n;base+=tile) {
   uint length=uint(std::min<size_t>(tile,n-base));
   uint offset=uint(base);
   void *arguments[]={&g.data,&b.data,&m.data,&pixels.data,&rng.data,&p,&frame.data,&accum.data,&offset,&length};
   check(cudaLaunchKernel(reinterpret_cast<const void*>(trace_pixels),dim3((length+127)/128),dim3(128),arguments,0,nullptr),"trace launch");
   check(cudaGetLastError(),"trace launch");check(cudaDeviceSynchronize(),"trace completion");
  }
  std::cout<<"pass="<<pass+1<<"/"<<passes<<std::endl;
 }
 double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 auto p=s.params;p.filter.z=1.f/passes;
 for(size_t base=0;base<n;base+=tile) {
  uint length=uint(std::min<size_t>(tile,n-base));
  uint offset=uint(base);void *arguments[]={&accum.data,&bgra.data,&p,&m.data,&offset,&length};
  check(cudaLaunchKernel(reinterpret_cast<const void*>(encode_pixels),dim3((length+127)/128),dim3(128),arguments,0,nullptr),"display launch");
 }
 check(cudaDeviceSynchronize(),"display completion");
 std::vector<pt::uchar4> image(n);check(cudaMemcpy(image.data(),bgra.data,n*sizeof(pt::uchar4),cudaMemcpyDeviceToHost),"download display bytes");
 pt::write_ppm(argv[2],image,p.image.x,p.image.y);
 if(argc>5) {std::vector<pt::float4> linear(n);check(cudaMemcpy(linear.data(),accum.data,n*sizeof(pt::float4),cudaMemcpyDeviceToHost),"download linear image");pt::write_linear(argv[5],linear,1.f/passes);}
 std::ofstream log(std::string(argv[2])+".json");if(!log) throw std::runtime_error("Cannot write render report");
 log<<"{\n  \"backend\": \"isolated Clang CUDA\",\n  \"gpu\": \""<<prop.name<<"\",\n  \"width\": "<<p.image.x<<",\n  \"height\": "<<p.image.y<<",\n  \"spp\": "<<uint64_t(p.image.z)*passes<<",\n  \"depth\": "<<p.image.w<<",\n  \"render_seconds\": "<<seconds<<",\n  \"pure_path_tracing\": true\n}\n";
 if(!log) throw std::runtime_error("Render report write failed");
 std::cout<<"Rendered "<<uint64_t(p.image.z)*passes<<" spp in "<<seconds<<" seconds\n";return 0;
 }catch(const std::exception &e) {std::cerr<<e.what()<<"\n";return 1;}}
