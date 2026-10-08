#pragma once
// CPU-only asset loading, shared by both backends.
#include "../third_party/tiny_obj_loader.h"
#include "../third_party/stb_image.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace assets {
struct V3 { float x = 0, y = 0, z = 0; };
struct UV { float u = 0, v = 0; };
struct Triangle {
    V3 p[3], n[3];
    UV uv[3];
    bool has_uv = false, has_normals = false;
    std::uint32_t material_id = 0;
};
struct Material {
    std::string name, image_path;
    V3 diffuse{1, 1, 1};
    float u_scale = 1, v_scale = 1, u_offset = 0, v_offset = 0;
    bool clamp = false, srgb = true;
};
struct Mesh { std::vector<Triangle> triangles; std::vector<Material> materials; };
struct Image { int width = 0, height = 0; std::vector<unsigned char> rgba; };
inline std::filesystem::path portable_path(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return std::filesystem::path(path);
}
// The image path is relative to its declaring MTL, including MTLs in subdirectories.
class MaterialReader : public tinyobj::MaterialReader {
    std::filesystem::path directory_;
public:
    explicit MaterialReader(std::filesystem::path directory) : directory_(std::move(directory)) {}
    bool operator()(const std::string &name, std::vector<tinyobj::material_t> *materials,
                    std::map<std::string, int> *map, std::string *warn, std::string *err) override
    {
        auto path = portable_path(name);
        if (path.is_relative()) path = directory_ / path;
        std::ifstream file(path);
        if (!file) {
            if (err) *err += "Cannot open MTL: " + path.string() + "\n";
            return false;
        }
        const auto first = materials->size();
        tinyobj::LoadMtl(map, materials, &file, warn, err);
        for (auto i = first; i < materials->size(); ++i) {
            auto &name_ref = (*materials)[i].diffuse_texname;
            if (name_ref.empty()) continue;
            auto image = portable_path(name_ref);
            if (image.is_relative()) image = path.parent_path() / image;
            name_ref = image.lexically_normal().string();
        }
        return true;
    }
};
inline V3 vector_at(const std::vector<tinyobj::real_t> &values, int index)
{
    if (index < 0) throw std::runtime_error("Missing OBJ position/normal index");
    const auto i = static_cast<std::size_t>(index) * 3;
    V3 v{float(values.at(i)), float(values.at(i + 1)), float(values.at(i + 2))};
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        throw std::runtime_error("Non-finite OBJ position/normal");
    return v;
}
inline Mesh load_obj(const std::string &filename)
{
    const auto path = std::filesystem::absolute(portable_path(filename));
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open OBJ: " + path.string());
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> source_materials;
    MaterialReader reader(path.parent_path());
    std::string warning, error;
    const bool ok = tinyobj::LoadObj(&attrib, &shapes, &source_materials, &warning, &error,
                                    &file, &reader, false, false);
    if (!ok || !error.empty()) throw std::runtime_error("OBJ load failed: " + error);
    if (!warning.empty()) std::cerr << warning;
    Mesh mesh;
    Material fallback;
    fallback.name = "__default";
    fallback.diffuse = {0.7f, 0.7f, 0.7f};
    mesh.materials.push_back(fallback);
    if (source_materials.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Too many OBJ materials");
    for (const auto &s : source_materials) {
        Material m;
        m.name = s.name;
        m.diffuse = {float(s.diffuse[0]), float(s.diffuse[1]), float(s.diffuse[2])};
        for (float c : {m.diffuse.x, m.diffuse.y, m.diffuse.z})
            if (!std::isfinite(c) || c < 0 || c > 1)
                throw std::runtime_error("Diffuse color outside [0,1]: " + m.name);
        m.image_path = s.diffuse_texname;
        m.u_scale = float(s.diffuse_texopt.scale[0]);
        m.v_scale = float(s.diffuse_texopt.scale[1]);
        m.u_offset = float(s.diffuse_texopt.origin_offset[0]);
        m.v_offset = float(s.diffuse_texopt.origin_offset[1]);
        for (float c : {m.u_scale, m.v_scale, m.u_offset, m.v_offset})
            if (!std::isfinite(c)) throw std::runtime_error("Invalid MTL UV transform");
        m.clamp = s.diffuse_texopt.clamp;
        const auto &space = s.diffuse_texopt.colorspace;
        if (space == "linear") m.srgb = false;
        else if (!space.empty() && space != "sRGB" && space != "srgb")
            throw std::runtime_error("Unsupported base-color space: " + space);
        mesh.materials.push_back(m);
    }
    for (const auto &shape : shapes) {
        std::size_t offset = 0;
        for (std::size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face) {
            if (shape.mesh.num_face_vertices[face] != 3)
                throw std::runtime_error("Export triangulated OBJ faces first");
            Triangle t;
            t.has_uv = t.has_normals = true;
            for (int k = 0; k < 3; ++k) {
                const auto &idx = shape.mesh.indices.at(offset + k);
                t.p[k] = vector_at(attrib.vertices, idx.vertex_index);
                if (idx.texcoord_index < 0) t.has_uv = false;
                else {
                    const auto i = static_cast<std::size_t>(idx.texcoord_index) * 2;
                    t.uv[k] = {float(attrib.texcoords.at(i)), float(attrib.texcoords.at(i + 1))};
                    if (!std::isfinite(t.uv[k].u) || !std::isfinite(t.uv[k].v))
                        throw std::runtime_error("Non-finite OBJ UV");
                }
                if (idx.normal_index < 0) t.has_normals = false;
                else {
                    V3 n = vector_at(attrib.normals, idx.normal_index);
                    const float length2 = n.x*n.x + n.y*n.y + n.z*n.z;
                    if (!std::isfinite(length2) || length2 <= 1e-20f)
                        throw std::runtime_error("Zero/invalid OBJ normal");
                    const float inv = 1.0f / std::sqrt(length2);
                    t.n[k] = {n.x*inv, n.y*inv, n.z*inv};
                }
            }
            offset += 3;
            const V3 e1{t.p[1].x-t.p[0].x, t.p[1].y-t.p[0].y, t.p[1].z-t.p[0].z};
            const V3 e2{t.p[2].x-t.p[0].x, t.p[2].y-t.p[0].y, t.p[2].z-t.p[0].z};
            const V3 n{e1.y*e2.z-e1.z*e2.y, e1.z*e2.x-e1.x*e2.z, e1.x*e2.y-e1.y*e2.x};
            const float area2 = n.x*n.x+n.y*n.y+n.z*n.z;
            if (!std::isfinite(area2) || area2 <= 0)
                throw std::runtime_error("Zero-area/non-finite OBJ triangle");
            const int id = shape.mesh.material_ids.at(face);
            if (id < -1 || id >= static_cast<int>(source_materials.size()))
                throw std::runtime_error("OBJ material index out of range");
            t.material_id = id < 0 ? 0u : static_cast<std::uint32_t>(id + 1);
            if (!t.has_uv && !mesh.materials[t.material_id].image_path.empty())
                throw std::runtime_error("Missing UV on textured face: " + mesh.materials[t.material_id].name);
            mesh.triangles.push_back(t);
        }
    }
    if (mesh.triangles.empty()) throw std::runtime_error("OBJ contains no triangles");
    return mesh;
}
inline Image load_image(const std::string &path)
{
    Image image;
    int channels = 0;
    unsigned char *pixels = stbi_load(path.c_str(), &image.width, &image.height, &channels, 4);
    if (!pixels) throw std::runtime_error("Cannot load image: " + path + " (" +
        (stbi_failure_reason() ? stbi_failure_reason() : "unknown error") + ")");
    const std::size_t width = static_cast<std::size_t>(image.width);
    const std::size_t height = static_cast<std::size_t>(image.height);
    if (image.width <= 0 || image.height <= 0 ||
        width > std::numeric_limits<std::size_t>::max() / 4 / height) {
        stbi_image_free(pixels);
        throw std::runtime_error("Invalid image dimensions");
    }
    try { image.rgba.assign(pixels, pixels + width * height * 4); }
    catch (...) { stbi_image_free(pixels); throw; }
    stbi_image_free(pixels);
    return image;
}
} // namespace assets
