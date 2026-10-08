#pragma once

#include "vec3.h"
#include <type_traits>
#include <string>
#include <vector>

struct uv2 { float u = 0.0f, v = 0.0f; };

struct triangle_data
{
    vec3 a;
    vec3 b;
    vec3 c;
    uv2 uv[3];
    vec3 n[3] = {vec3(0, 0, 0), vec3(0, 0, 0), vec3(0, 0, 0)};
    bool has_uv = false;
    bool has_normals = false;
    int material_id = 0;
};

static_assert(
    std::is_trivially_copyable<triangle_data>::value,
    "triangle_data must support byte copying");

struct material_data_cpu {
    std::string name, base_color_path;
    vec3 diffuse = vec3(1, 1, 1);
    float u_scale = 1, v_scale = 1, u_offset = 0, v_offset = 0;
    bool clamp = false, srgb = true;
};
struct mesh_data_cpu {
    std::vector<triangle_data> triangles;
    std::vector<material_data_cpu> materials;
};
