#pragma once
#include "launch.h"
#include <vector>
#include <algorithm>
#include <stdexcept>
namespace rt {
// Preserve the old pass -> tile order and per-pixel RNG/accumulation sequence.
inline std::vector<Launch> launch_plan(Launch base,const pt::GPUParams &scene,
                                      size_t pixels,unsigned tile,unsigned passes) {
    if(!tile || !passes || !pixels) throw std::runtime_error("Empty launch plan");
    std::vector<Launch> result;
    result.reserve((size_t(passes)+1)*((pixels+tile-1)/tile));
    auto append=[&] {
        for(size_t offset=0;offset<pixels;offset+=tile) {
            base.base=unsigned(offset);
            base.length=unsigned(std::min<size_t>(tile,pixels-offset));
            result.push_back(base);
        }
    };
    for(unsigned pass=0;pass<passes;++pass) {
        base.params=scene;
        base.params.counts.z=pass+1;
        base.params.counts.w=pass;
        base.params.filter.z=1.f/(pass+1);
        append();
    }
    base.verify=0;
    base.params.filter.z=1.f/passes;
    append();
    return result;
}
} // namespace rt
