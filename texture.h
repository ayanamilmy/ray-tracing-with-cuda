#pragma once
#include "vec3.h"
#include <cstddef>
#include <type_traits>
struct texture_view {
    const unsigned char *rgba = nullptr;
    int width = 0, height = 0;
    float u_scale = 1, v_scale = 1, u_offset = 0, v_offset = 0;
    bool clamp = false, srgb = true;
};
struct material_data_gpu { vec3 diffuse; texture_view image; };
static_assert(std::is_trivially_copyable<material_data_gpu>::value,
              "Material transfer data must support byte copying");
__device__ inline float srgb_to_linear(float c)
{
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
__device__ inline vec3 sample_base_color(const texture_view &image, float u, float v)
{
    if (!image.rgba || image.width <= 0 || image.height <= 0) return vec3(1, 0, 1);
    u = u * image.u_scale + image.u_offset;
    v = v * image.v_scale + image.v_offset;
    // Reject invalid UVs before converting them to integer pixel indices.
    if (!isfinite(u) || !isfinite(v)) return vec3(1, 0, 1);
    if (image.clamp) {
        u = fminf(fmaxf(u, 0.0f), 1.0f); v = fminf(fmaxf(v, 0.0f), 1.0f);
    } else { u -= floorf(u); v -= floorf(v); }
    int x = static_cast<int>(floorf(u * image.width));
    int y = static_cast<int>(floorf((1.0f - v) * image.height));
    if (image.clamp) {
        if (x >= image.width) x = image.width - 1;
        if (y >= image.height) y = image.height - 1;
    } else { x %= image.width; y %= image.height; }
    const std::size_t i = 4 * (static_cast<std::size_t>(y) * image.width + x);
    vec3 c(image.rgba[i] / 255.0f, image.rgba[i+1] / 255.0f, image.rgba[i+2] / 255.0f);
    return image.srgb ? vec3(srgb_to_linear(c.r()), srgb_to_linear(c.g()), srgb_to_linear(c.b())) : c;
}
