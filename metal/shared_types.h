#pragma once
#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
using F4 = float4;
using U4 = uint4;
#else
#include <simd/simd.h>
#include <cstdint>
#include <cstddef>
using F4 = simd_float4;
using U4 = simd_uint4;
#endif

// All fields are 16-byte vectors, so C++ and Metal agree on buffer layout.
struct GPUPrimitive {
    F4 a, b, c;             // Sphere: a.xyz=center, a.w=radius.
    F4 n0, n1, n2;
    F4 uv01, uv2;
    F4 outline0, outline1, outline2; // Authored smooth hull directions; w: local width mask.
    F4 tangent0, tangent1, tangent2; // Optional original FBX tangent xyz + handedness.
    F4 vertex_color0, vertex_color1, vertex_color2;
    F4 face0, face1, face2; // UV1.xy, vertex face flag, vertex material ID (ZZZATTR2).
    F4 extra_uv0, extra_uv1, extra_uv2; // Original UV2.xy/UV3.zw per vertex (ZZZATTR3).
    U4 meta;                // x: sphere/triangle; y: material; z: UV/normal bits; w: bit0 FBX tangent/color, bit1 face UV/flags.
};
struct GPUToonProfile {
    F4 bands;              // shadow threshold, mid threshold, shadow level, mid level.
    F4 lobe;               // elliptical width scales xy, specular scale, roughness floor.
    F4 tint;               // shadow tint RGB; source shadow-color blend (0 neutral, 1 source).
};
struct GPUAreaLight {
    F4 center;             // center.xyz, full area in w.
    F4 u, v;               // world-space half-width and half-height vectors.
    F4 emission;           // linear radiance RGB, reserved.
};
struct GPUMaterial {
    F4 color;               // xyz: albedo/emission; w: fuzz or IOR.
    F4 uv_transform;        // scale_u, scale_v, offset_u, offset_v.
    U4 image;               // byte offset, width, height, present.
    U4 flags;               // kind: 0 diffuse/1 metal/2 glass/3 light/4 MMD; clamp; sRGB.
    U4 toon_image;          // Optional MMD ramp: byte offset, width, height, present.
    U4 sphere_image;        // Optional view-normal sphere map, same layout.
    F4 mmd;                // opacity, toon strength, sphere strength, additive overlay.
    U4 mmd_flags;          // sphere mode (0/1/2), receive shadow, cast shadow, double sided.
    U4 normal_image, metallic_image, roughness_image; // Linear control maps, base UV.
    F4 surface;             // metallic factor, roughness factor, normal strength, reflection mix.
    U4 surface_flags;       // metallic channel, roughness channel, flip normal green, reserved.
    F4 edge_color;          // Linear RGBA, read from PMX.
    F4 edge;                // enabled, HoyoToon external width * 0.0015, reserved, reserved.
    // ZZZ packed-control material. Arrays use descending ID bands (>=.8 .. <.2).
    F4 zzz_color[5], zzz_shallow[5], zzz_shadow[5], zzz_specular[5], zzz_outline[5];
    F4 zzz_region[5];        // highlight shape, softness, range, ToonSpecular*ModelSize.
    F4 zzz_params;           // metallic, glossiness, specular intensity, bump scale.
    F4 zzz_post[6];          // shallow/fade, shadow/fade, front, SSS post-tints.
    F4 zzz_head;             // optional spherical highlight normal centre and radius.
    U4 zzz_face_image;      // Linear SDF.R / highlight.G / legacy outline.B / chin AO.A.
    F4 zzz_head_forward, zzz_head_right; // Static world-space head axes.
    F4 zzz_face_shadow;     // Hair-shadow tint RGB; cone radius in radians.
    F4 zzz_face_detail;     // face highlight scale, nose smooth x/y, extended enabled.
    U4 zzz_render;          // opacity mode 0/1/2, alpha source 0 D.a/1 A.r, cull 0/1/2, eye role.
    F4 zzz_alpha;           // cutoff, minimum hair stencil alpha, eye reveal depth, double UV enabled.
    U4 zzz_eye_lut;         // Optional 16x16 eye-shadow/highlight LUT; no automatic filename guess.
    U4 zzz_matcap_images[5];
    F4 zzz_matcap_tint[5], zzz_matcap_params[5]; // color/alpha burst, blend mode, texture ID gate.
    F4 zzz_matcap_motion[5], zzz_matcap_refract[5]; // speed xy/refract/depth; UV scale/offset.
    F4 zzz_rim_colors[5], zzz_sun_colors[5];
    F4 zzz_effects;         // MatCap, rim, rim width, effect time (explicit, not wall clock).
    F4 zzz_misc;             // albedo smoothness, type, alpha enabled, ZZZ enabled.
    F4 zzz_source;           // source SkinMatId, SpecularHighlights, UseBumpMap, reserved.
    F4 zzz_response[5];     // Original per-region albedo softness, brightness, ambient scale.
    F4 zzz_emission[5];
    F4 zzz_emission_flags;  // Original emission/secondary emission enables; reserved.
    U4 zzz_uv_rules;        // HoyoToon DoubleUV, SymmetryUV, LegacyOtherData, UseLegacyFace.
    U4 zzz_outline_rules;   // HoyoToon NormalUV, UseLightMapOL, DisableFOVScalingOL, OutlineZOff.
    F4 zzz_designed;        // Authored extension: anisotropy, nose yaw/down cosines, present bits (1/2).
    GPUToonProfile toon_profiles[5], toon_metal; // Surface-local region rules and metallic override.
};
struct GPUNode {
    F4 lo, hi;
    U4 link;                // left, right, primitive, is_leaf.
};
struct GPURandom { U4 state, extra; };
struct GPUParams {
    F4 origin, lower_left, horizontal, vertical, right, up;
    U4 image;               // width, height, samples/frame, max depth.
    U4 counts;              // primitives, nodes, accumulated frames, frame index.
    U4 settings;            // paused, bilateral enabled, MMD enabled, disable hair overlay.
    F4 filter;              // spatial sigma, range sigma, inverse frame count, outline BVH padding.
    F4 toon_light;          // xyz: directional light toward source; w: override enabled.
    U4 extensions;          // extended ZZZ compositor; indirect strength float bits; transport 0 legacy/1 GGX/2 Toon; disable Toon BSDF.
    U4 features;            // outlines, normal maps, surface shading, ZZZ comparison flags.
    F4 lighting;            // directional radiance, sky multiplier, display exposure, sphere multiplier.
    U4 rendering;          // rectangular lights, tonemap, authored Toon profiles, bit0 source-material path / bit1 neutral sky.
    F4 tone;               // linear shoulder knee, reserved.
    GPUAreaLight area_lights[4];
};
#ifndef __METAL_VERSION__
static_assert(sizeof(GPUToonProfile) == 48);
static_assert(sizeof(GPUAreaLight) == 64);
static_assert(sizeof(GPUPrimitive) == 384);
static_assert(sizeof(GPUMaterial) == 2096);
static_assert(sizeof(GPUNode) == 48);
static_assert(sizeof(GPURandom) == 32);
static_assert(sizeof(GPUParams) == 512);
static_assert(offsetof(GPUPrimitive, meta) == 368);
static_assert(offsetof(GPUParams, filter) == 144);
#endif
