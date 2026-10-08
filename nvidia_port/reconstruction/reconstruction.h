#pragma once
#include <cuda_runtime.h>
#include <optix.h>
#include <memory>
#include <string>

namespace reconstruction {
// A CUDA-only interface: no original renderer material/geometry headers are changed.
struct Frame {
    void *accum, *albedo, *normal, *motion, *depth, *roughness, *specular;
    float world_to_view[16], view_to_clip[16]; // NGX row-major matrices
    float jitter_x=0, jitter_y=0, delta_ms=1000.f/120;
    unsigned passes=1;
    bool reset=true;
};
class Processor {
public:
    Processor(OptixDeviceContext, std::string mode, unsigned width, unsigned height,
              unsigned output_width, unsigned output_height, cudaStream_t);
    ~Processor();
    Processor(const Processor&)=delete;
    Processor& operator=(const Processor&)=delete;
    void *run(const Frame&);
    const char *mode() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
void probe_dlss();
void optimal_dlss(unsigned width,unsigned height);
void prepare_random(void *rng,size_t pixels,unsigned frame_seed,cudaStream_t);
} // namespace reconstruction
