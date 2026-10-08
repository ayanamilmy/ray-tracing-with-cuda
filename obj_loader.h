#pragma once
#include "mesh_data.h"
#include "shared/asset_io.h"

inline mesh_data_cpu load_obj_mesh(const std::string &path)
{
    const assets::Mesh source = assets::load_obj(path);
    mesh_data_cpu result;
    result.triangles.reserve(source.triangles.size());
    result.materials.reserve(source.materials.size());
    const auto v3 = [](const assets::V3 &v) { return vec3(v.x, v.y, v.z); };
    for (const auto &s : source.triangles) {
        triangle_data t;
        t.a = v3(s.p[0]); t.b = v3(s.p[1]); t.c = v3(s.p[2]);
        for (int k = 0; k < 3; ++k) {
            t.uv[k] = {s.uv[k].u, s.uv[k].v};
            t.n[k] = v3(s.n[k]);
        }
        t.has_uv = s.has_uv; t.has_normals = s.has_normals;
        t.material_id = static_cast<int>(s.material_id);
        result.triangles.push_back(t);
    }
    for (const auto &s : source.materials) {
        material_data_cpu m;
        m.name = s.name; m.base_color_path = s.image_path; m.diffuse = v3(s.diffuse);
        m.u_scale = s.u_scale; m.v_scale = s.v_scale;
        m.u_offset = s.u_offset; m.v_offset = s.v_offset;
        m.clamp = s.clamp; m.srgb = s.srgb;
        result.materials.push_back(m);
    }
    return result;
}
