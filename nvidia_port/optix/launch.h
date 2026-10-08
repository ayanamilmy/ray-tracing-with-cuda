#pragma once
#include <optix.h>
#include "../include/types.h"
namespace rt {
struct Launch {
    OptixTraversableHandle handle;
    const pt::GPUPrimitive *primitives;
    const pt::GPUNode *nodes; // Reference validation only, not traversed by normal RT launches.
    const pt::GPUMaterial *materials;
    const pt::uchar *pixels;
    pt::GPURandom *rng;
    pt::float4 *frame, *accum;
    pt::uchar4 *bgra;
    pt::GPUParams params;
    unsigned base, length;
    unsigned verify, software;
    unsigned *errors;
};
struct HitData { const unsigned *indices; };
static_assert(sizeof(Launch)==624 && alignof(Launch)==16);
static_assert(offsetof(Launch,params)==80 && offsetof(Launch,base)==592 && offsetof(Launch,errors)==608);
}
