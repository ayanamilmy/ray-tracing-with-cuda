#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include "../third_party/tiny_obj_loader.h"
#undef TINYOBJLOADER_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#include "../shared/asset_io.h"
#include "shared_types.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <unordered_map>
#include <map>
#include <tuple>
#include <fstream>
#include <array>

using V3 = simd_float3;
namespace fs = std::filesystem;
static F4 f4(V3 v, float w = 0) { return {v.x, v.y, v.z, w}; }
static V3 v3(assets::V3 v) { return {v.x, v.y, v.z}; }
static V3 normalized(V3 v) { return simd_normalize(v); }
static V3 minimum(V3 a, V3 b) { return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)}; }
static V3 maximum(V3 a, V3 b) { return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)}; }

struct Config {
    int width=720, height=360, samples=4, depth=50;
    float seconds=0, sigma=0.15f, aperture=0.1f, focus=10, fov=20;
    int frames=0,trace_tile_pixels=1024;
    bool no_ground=false,headless=false,check=false,character_scene=false,no_toon=false,no_hair_highlight=false;
    bool no_outline=false,no_normal=false,no_pbr=false,physical_character=false,legacy_character=false;
    float outline_scale=1, indirect_strength=0.35f;
    float sun_intensity=2.8f,environment_strength=0.25f,exposure=1,sphere_intensity=1;
    unsigned zzz_comparison=0;
    V3 toon_light{0,1,0};bool toon_light_set=false;
    bool camera_set=false,target_set=false;
    V3 camera{0.90f,1.05f,3.75f}, target{0.03f,0.87f,-0.10f};
    bool tonemap=true,uniform_toon=false,environment_set=false,asset_toon=false,neutral_sky=false,strict_bindings=false,dynamic_hulls=false;
    float tone_knee=0.7f, sun_angle=0, ground_albedo=0.5f;
    std::vector<GPUAreaLight> area_lights;
    std::string obj, output, mmd_materials, toon_profiles;
};
static void usage()
{
    std::cout << "metal_accum [spp/frame] [seconds] [sigma_r] [width] [height] [OBJ]\n"
        "  --headless --frames N --output image.png\n"
        "  --depth N --aperture F --focus F --fov F\n"
        "  --toon-path-tracing (default character mode: every hit uses Toon BSDF)\n"
        "  --legacy-character --indirect-strength F (old hybrid comparison only)\n"
        "  --sun-intensity F --environment-strength F --sphere-intensity F --exposure F\n"
        "  --sun-angle F (angular radius in degrees, 0 delta; .1..60 finite emitter)\n"
        "  --no-ground (omit ground geometry)\n"
        "  --ground-albedo F (linear gray reflectance, default .5)\n"
        "  --no-tonemap --tone-knee F (default .7; preserve colors below knee)\n"
        "  --toon-profiles path.json --uniform-toon (old uniform rules comparison)\n"
        "  --area-light CX CY CZ TX TY TZ WIDTH HEIGHT R G B INTENSITY (repeat, max 4)\n"
        "  --physical-character (full BSDF paths, no Toon/MatCap/outline overlay)\n"
        "  --mmd-materials character.mmd.json --character-scene\n"
        "  --no-toon --no-hair-highlight (MMD comparison switches)\n"
        "  --no-outline --no-normal --no-pbr --outline-scale F\n"
        "  --no-light-control --no-material-id --no-specular-mask --no-metallic (ZZZ checks)\n"
        "  --no-face-sdf --no-hair-shadow --light-direction X Y Z (direction toward sun in full path mode)\n"
        "  --no-face-highlight --no-nose-line --no-eye-layers --no-transparency --no-matcap --no-rim\n"
        "  --debug-alpha --debug-matcap-mask --debug-rim\n"
        "  --debug-face-sdf --debug-hair-shadow (grayscale face controls)\n"
        "  --camera X Y Z --target X Y Z\n"
        "  --asset-toon (raw source JSON and all prepared texture bindings; full path integration)\n"
        "  --strict-texture-bindings (opt-in: omit unconfirmed textures) --no-emission\n"
        "  --no-anisotropy --no-dynamic-nose (disable authored source-parameter extensions)\n"
        "  --neutral-sky (white environment; render setup, not recovered game lighting)\n"
        "  --check (compile GPU pipelines only; no scene or rendering)\n"
        "  RMB look; MMB pan; wheel dolly; RMB+WASD/QE fly; F focus; SPACE accumulate; P save; ESC exit\n";
}
static Config parse_args(int argc, char **argv)
{
    Config c;
    std::vector<std::string> positional;
    for (int i=1;i<argc;++i) {
        std::string arg=argv[i];
        const auto value=[&]() -> std::string {
            if (++i>=argc) throw std::runtime_error("Missing value for "+arg);
            return argv[i];
        };
        if (arg=="--headless") c.headless=true;
        else if (arg=="--no-ground") c.no_ground=true;
        else if (arg=="--check") c.check=true;
        else if (arg=="--frames") c.frames=std::stoi(value());
        else if (arg=="--output") c.output=value();
        else if (arg=="--depth") c.depth=std::stoi(value());
        else if (arg=="--trace-tile-pixels") {c.trace_tile_pixels=std::stoi(value());if(c.trace_tile_pixels<=0 || c.trace_tile_pixels>1048576) throw std::runtime_error("Invalid trace tile budget");}
        else if (arg=="--aperture") c.aperture=std::stof(value());
        else if (arg=="--focus") c.focus=std::stof(value());
        else if (arg=="--fov") c.fov=std::stof(value());
        else if (arg=="--mmd-materials") c.mmd_materials=value();
        else if (arg=="--character-scene") c.character_scene=true;
        else if (arg=="--no-toon") c.no_toon=true;
        else if (arg=="--no-hair-highlight") c.no_hair_highlight=true;
        else if (arg=="--no-outline") c.no_outline=true;
        else if (arg=="--no-normal") c.no_normal=true;
        else if (arg=="--no-pbr") c.no_pbr=true;
        else if (arg=="--no-light-control") c.zzz_comparison|=1;
        else if (arg=="--no-material-id") c.zzz_comparison|=2;
        else if (arg=="--no-specular-mask") c.zzz_comparison|=4;
        else if (arg=="--no-metallic") c.zzz_comparison|=8;
        else if (arg=="--no-face-sdf") c.zzz_comparison|=16;
        else if (arg=="--no-hair-shadow") c.zzz_comparison|=32;
        else if (arg=="--debug-face-sdf") c.zzz_comparison|=64;
        else if (arg=="--debug-hair-shadow") c.zzz_comparison|=128;
        else if (arg=="--no-face-highlight") c.zzz_comparison|=256;
        else if (arg=="--no-nose-line") c.zzz_comparison|=512;
        else if (arg=="--no-eye-layers") c.zzz_comparison|=1024;
        else if (arg=="--no-transparency") c.zzz_comparison|=2048;
        else if (arg=="--no-matcap") c.zzz_comparison|=4096;
        else if (arg=="--no-rim") c.zzz_comparison|=8192;
        else if (arg=="--debug-alpha") c.zzz_comparison|=16384;
        else if (arg=="--debug-matcap-mask") c.zzz_comparison|=32768;
        else if (arg=="--debug-rim") c.zzz_comparison|=65536;
        else if (arg=="--debug-eye-layers") c.zzz_comparison|=131072;
        else if (arg=="--light-direction") {
            const float x=std::stof(value()),y=std::stof(value()),z=std::stof(value());
            c.toon_light={x,y,z};c.toon_light_set=true;
        }
        else if (arg=="--physical-character") { c.physical_character=true; c.legacy_character=false; }
        else if (arg=="--legacy-character") { c.legacy_character=true; c.physical_character=false; }
        else if (arg=="--toon-path-tracing") { c.physical_character=false; c.legacy_character=false; }
        else if (arg=="--sun-intensity") c.sun_intensity=std::stof(value());
        else if (arg=="--sun-angle") c.sun_angle=std::stof(value());
        else if (arg=="--ground-albedo") c.ground_albedo=std::stof(value());
        else if (arg=="--environment-strength") { c.environment_strength=std::stof(value()); c.environment_set=true; }
        else if (arg=="--sphere-intensity") c.sphere_intensity=std::stof(value());
        else if (arg=="--exposure") c.exposure=std::stof(value());
        else if (arg=="--no-tonemap") c.tonemap=false;
        else if (arg=="--tone-knee") c.tone_knee=std::stof(value());
        else if (arg=="--toon-profiles") c.toon_profiles=value();
        else if (arg=="--uniform-toon") c.uniform_toon=true;
        else if (arg=="--asset-toon") c.asset_toon=true;
        else if (arg=="--strict-texture-bindings") c.strict_bindings=true;
        else if (arg=="--dynamic-hulls") c.dynamic_hulls=true;
        else if (arg=="--no-emission") c.zzz_comparison|=262144;
        else if (arg=="--no-anisotropy") c.zzz_comparison|=524288;
        else if (arg=="--no-dynamic-nose") c.zzz_comparison|=1048576;
        else if (arg=="--neutral-sky") c.neutral_sky=true;
        else if (arg=="--area-light") {
            float a[12]; for (float &x:a) x=std::stof(value());
            for (float x:a) if (!std::isfinite(x)) throw std::runtime_error("Non-finite area light");
            V3 center{a[0],a[1],a[2]},target{a[3],a[4],a[5]};
            if (c.area_lights.size()>=4 || simd_length(target-center)<0.001f || a[6]<=0 || a[7]<=0 ||
                a[6]>100 || a[7]>100 || a[8]<0 || a[9]<0 || a[10]<0 || a[11]<0 ||
                a[8]>10 || a[9]>10 || a[10]>10 || a[11]>10000) throw std::runtime_error("Invalid area light");
            V3 normal=normalized(target-center),axis=std::abs(normal.y)<0.95f ? V3{0,1,0} : V3{1,0,0};
            V3 u=normalized(simd_cross(axis,normal)),v=simd_cross(normal,u);
            GPUAreaLight light{};light.center=f4(center,a[6]*a[7]);light.u=f4(u*(a[6]*.5f));light.v=f4(v*(a[7]*.5f));
            light.emission={a[8]*a[11],a[9]*a[11],a[10]*a[11],0};c.area_lights.push_back(light);
        }
        else if (arg=="--indirect-strength") c.indirect_strength=std::stof(value());
        else if (arg=="--outline-scale") c.outline_scale=std::stof(value());
        else if (arg=="--camera" || arg=="--target") {
            const float x=std::stof(value()),y=std::stof(value()),z=std::stof(value());
            if (arg=="--camera") {c.camera={x,y,z};c.camera_set=true;}
            else {c.target={x,y,z};c.target_set=true;}
        }
        else if (arg=="--help") { usage(); std::exit(0); }
        else if (arg.rfind("--",0)==0) throw std::runtime_error("Unknown argument: "+arg);
        else positional.push_back(arg);
    }
    if (positional.size()>6) throw std::runtime_error("Too many positional arguments");
    if (positional.size()>0) c.samples=std::stoi(positional[0]);
    if (positional.size()>1) c.seconds=std::stof(positional[1]);
    if (positional.size()>2) c.sigma=std::stof(positional[2]);
    if (positional.size()>3) c.width=std::stoi(positional[3]);
    if (positional.size()>4) c.height=std::stoi(positional[4]);
    if (positional.size()>5) c.obj=positional[5];
    if (!std::isfinite(c.indirect_strength) || c.indirect_strength<0 || c.indirect_strength>2)
        throw std::runtime_error("Indirect strength must be between 0 and 2");
    const int n=int(std::sqrt(double(std::max(c.samples,0))));
    if (c.width<=0 || c.height<=0 || c.samples<=0 || n*n!=c.samples || c.depth<=0 ||
        c.frames<0 || !std::isfinite(c.seconds) || c.seconds<0 ||
        !std::isfinite(c.sigma) || c.sigma<0 || !std::isfinite(c.aperture) || c.aperture<0 ||
        !std::isfinite(c.focus) || c.focus<=0 || !std::isfinite(c.fov) || c.fov<=0 || c.fov>=179)
        throw std::runtime_error("Invalid dimensions, square sample count, or rendering parameter");
    if (c.headless) {
        if (c.frames==0 && c.seconds==0) c.frames=1;
        if (c.output.empty()) c.output="metal-render.png";
    }
    if (std::uint64_t(c.width)*std::uint64_t(c.height)>UINT32_MAX)
        throw std::runtime_error("Pixel count exceeds GPU index range");
    if (!c.mmd_materials.empty() && c.obj.empty())
        throw std::runtime_error("MMD materials require an OBJ");
    if (c.asset_toon && (c.mmd_materials.empty() || c.legacy_character || c.physical_character || c.no_toon || !c.toon_profiles.empty()))
        throw std::runtime_error("Asset Toon requires full Toon paths and forbids authored profile overrides");
    if (!std::isfinite(c.outline_scale) || c.outline_scale<0 || c.outline_scale>10 ||
        !std::isfinite(c.camera.x) || !std::isfinite(c.camera.y) || !std::isfinite(c.camera.z) ||
        !std::isfinite(c.target.x) || !std::isfinite(c.target.y) || !std::isfinite(c.target.z) ||
        ((c.camera_set || c.target_set) && simd_length(c.camera-c.target)<0.001f) ||
        ((c.camera_set || c.target_set) && simd_length(simd_cross(c.target-c.camera,V3{0,1,0}))<0.001f))
        throw std::runtime_error("Invalid outline scale or camera");
    if (!std::isfinite(c.toon_light.x) || !std::isfinite(c.toon_light.y) || !std::isfinite(c.toon_light.z) ||
        simd_length_squared(c.toon_light)<1e-12f) throw std::runtime_error("Invalid Toon light direction");
    for (float x : {c.sun_intensity,c.environment_strength,c.sphere_intensity,c.exposure})
        if (!std::isfinite(x) || x<0 || x>100) throw std::runtime_error("Lighting values must be finite and between 0 and 100");
    if (!std::isfinite(c.sun_angle) || c.sun_angle<0 || c.sun_angle>60 || (c.sun_angle>0 && c.sun_angle<.1f) ||
        (c.sun_angle>0 && c.legacy_character)) throw std::runtime_error("Sun angle must be 0 or .1..60 degrees in full path mode");
    if (!std::isfinite(c.ground_albedo) || c.ground_albedo<0 || c.ground_albedo>1)
        throw std::runtime_error("Ground albedo must be between 0 and 1");
    // A real front/top directional emitter replaces the old camera-facing light rule.
    if (c.character_scene && !c.toon_light_set && !c.legacy_character) {
        c.toon_light={0,0.4f,1}; c.toon_light_set=true;
    }
    if (!std::isfinite(c.tone_knee) || c.tone_knee<0.1f || c.tone_knee>0.95f) throw std::runtime_error("Tone knee must be .1 to .95");
    c.toon_light=normalized(c.toon_light);
    return c;
}

#include "toon_profiles.h"

// Backend-local deterministic scene RNG. Sphere distributions and material rules match CUDA;
// CURAND and this generator do not promise the same individual random-ball positions.
struct Random {
    std::uint32_t state=1234;
    float next() {
        state ^= state<<13; state ^= state>>17; state ^= state<<5;
        return (float(state)+0.5f)*0x1p-32f;
    }
    V3 color() { float x=next(),y=next(),z=next(); return {x,y,z}; }
};
struct HostScene {
    std::vector<GPUPrimitive> primitives;
    std::vector<GPUMaterial> materials;
    std::vector<GPUNode> nodes;
    std::vector<unsigned char> pixels{0,0,0,255};
    std::vector<std::uint32_t> order;
    std::unordered_map<std::string,U4> images;
    ToonProfileLibrary profile_library;
    NSMutableDictionary *source_report=[NSMutableDictionary dictionary];
    U4 image(const fs::path &path) {
        const auto key=fs::absolute(path).lexically_normal().string();
        auto cached=images.find(key);
        if (cached!=images.end()) return cached->second;
        const auto loaded=assets::load_image(key);
        if (loaded.rgba.size()>UINT32_MAX-pixels.size())
            throw std::runtime_error("Texture atlas exceeds 4 GiB");
        U4 descriptor={unsigned(pixels.size()),unsigned(loaded.width),unsigned(loaded.height),1};
        pixels.insert(pixels.end(),loaded.rgba.begin(),loaded.rgba.end());
        images.emplace(key,descriptor);
        return descriptor;
    }
    void mmd_material(GPUMaterial &material, NSDictionary *entry, const fs::path &directory,
                      const Config &config) {
        const auto number=[&](NSString *key,float fallback) {
            id value=entry[key];
            if (!value) return fallback;
            if (![value isKindOfClass:[NSNumber class]])
                throw std::runtime_error("MMD field must be numeric: "+std::string(key.UTF8String));
            const float result=[value floatValue];
            if (!std::isfinite(result)) throw std::runtime_error("Non-finite MMD parameter");
            return result;
        };
        const auto texture=[&](NSString *key) -> U4 {
            id value=entry[key];
            if (!value || value==[NSNull null]) return U4{0,0,0,0};
            if (config.asset_toon && config.strict_bindings && [entry[@"zzz"] boolValue]) {
                // Family-name guesses cannot become exact Unity bindings merely
                // because they point to a real PNG. The package must record confirmation.
                NSDictionary *e=entry[@"binding_evidence"][key];
                if (![e[@"confirmed"] boolValue]) return U4{0,0,0,0};
            }
            if (![value isKindOfClass:[NSString class]]) throw std::runtime_error("Invalid MMD texture path");
            return image(directory/fs::path([(NSString *)value UTF8String]));
        };
        const float alpha=number(@"alpha",1),mode=number(@"sphere_mode",0);
        if (alpha<0 || alpha>1 || (mode!=0 && mode!=1 && mode!=2))
            throw std::runtime_error("Invalid MMD opacity or unsupported sphere mode (extra UV is disabled)");
        material.flags.x=4; // Independent MMD toon shading, not a Lambert BSDF.
        material.toon_image=texture(@"toon_texture");
        material.sphere_image=texture(@"sphere_texture");
        material.mmd={alpha,config.no_toon ? 0.0f : 1.0f,1.0f,number(@"additive_overlay",0)};
        material.mmd_flags={unsigned(mode),unsigned(number(@"self_shadow",1)!=0),
                            unsigned(number(@"casts_shadow",1)!=0),unsigned(number(@"double_sided",1)!=0)};
        material.normal_image=texture(@"normal_texture");
        material.metallic_image=texture(@"metallic_texture");
        material.roughness_image=texture(@"roughness_texture");
        const float metallic=number(@"metallic",0),roughness=number(@"roughness",0.7f);
        const float normal=number(@"normal_strength",1),reflection=number(@"reflection_mix",0);
        const float mc=number(@"metallic_channel",0),rc=number(@"roughness_channel",0);
        if (metallic<0 || metallic>1 || roughness<0 || roughness>1 || normal<0 || normal>4 ||
            reflection<0 || reflection>1 || mc<0 || mc>3 || mc!=std::floor(mc) ||
            rc<0 || rc>3 || rc!=std::floor(rc)) throw std::runtime_error("Invalid surface parameter");
        material.surface={metallic,roughness,normal,reflection};
        material.surface_flags={unsigned(mc),unsigned(rc),unsigned(number(@"normal_flip_y",0)!=0),0};
        const float edge_size=number(@"edge_size",0.5f);
        if (edge_size<0 || edge_size>10) throw std::runtime_error("Invalid edge size");
        material.edge={number(@"edge_enabled",0),edge_size*0.0015f*config.outline_scale,0,0};
        id edge=entry[@"edge_color"];
        if (edge) {
            if (![edge isKindOfClass:[NSArray class]] || [(NSArray *)edge count]!=4)
                throw std::runtime_error("Edge color must be RGBA");
            for (unsigned channel=0;channel<4;++channel) {
                id component=edge[channel];
                if (![component isKindOfClass:[NSNumber class]]) throw std::runtime_error("Invalid edge color");
                float v=[component floatValue];
                if (!std::isfinite(v) || v<0 || v>1) throw std::runtime_error("Invalid edge color");
                material.edge_color[channel]=channel==3 ? v : (v<=0.04045f ? v/12.92f : std::pow((v+0.055f)/1.055f,2.4f));
            }
        }
        if ([entry[@"zzz"] boolValue]) {
            NSDictionary *floats=entry[@"source_floats"],*colors=entry[@"source_colors"];
            NSMutableDictionary *scalar_defaults=[NSMutableDictionary dictionary],*color_defaults=[NSMutableDictionary dictionary];
            NSMutableArray *read_scalars=[NSMutableArray array],*read_colors=[NSMutableArray array];
            if (config.asset_toon) {
                NSString *source=entry[@"source_material_json"];
                if (![source isKindOfClass:[NSString class]]) throw std::runtime_error("Asset Toon requires original material JSON");
                fs::path source_path=fs::absolute(directory/fs::path(source.UTF8String));
                NSData *raw=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:source_path.c_str()]];
                id doc=raw ? [NSJSONSerialization JSONObjectWithData:raw options:0 error:nullptr] : nil;
                if (![doc isKindOfClass:[NSDictionary class]]) throw std::runtime_error("Cannot read original material JSON");
                floats=doc[@"m_SavedProperties"][@"m_Floats"];colors=doc[@"m_SavedProperties"][@"m_Colors"];
                if (![floats isKindOfClass:[NSDictionary class]] || ![colors isKindOfClass:[NSDictionary class]])
                    throw std::runtime_error("Original material JSON lacks scalar/color properties");
                NSMutableArray *omitted=[NSMutableArray array],*inferred=[NSMutableArray array],*loaded=[NSMutableArray array];
                for (NSString *field in @[@"normal_texture",@"metallic_texture",@"roughness_texture",@"face_lightmap",@"eye_color_map"])
                    if (entry[field] && entry[field]!=[NSNull null]) {
                        bool confirmed=[entry[@"binding_evidence"][field][@"confirmed"] boolValue];
                        if (config.strict_bindings && !confirmed) [omitted addObject:field];
                        else { [loaded addObject:field]; if (!confirmed) [inferred addObject:field]; }
                    }
                for (id slot in entry[@"matcap_textures"]) if (slot!=[NSNull null]) {
                    if (config.strict_bindings) [omitted addObject:@"MatCap candidate"];
                    else { [loaded addObject:@"MatCap candidate"];[inferred addObject:@"MatCap candidate"]; }
                }
                source_report[doc[@"m_Name"] ?: source]=@{@"source":source,@"floats":floats,@"colors":colors,
                    @"binding_evidence":entry[@"binding_evidence"] ?: @{},@"omitted_unresolved_bindings":omitted,
                    @"anonymous_texture_references":doc[@"m_SavedProperties"][@"m_TexEnvs"] ?: @{},
                    @"community_shader_scalar_defaults":scalar_defaults,@"community_shader_color_defaults":color_defaults,
                    @"backend_material_classification":entry[@"zzz_type"] ?: @0,
                    @"classification_source":@"Prepared package mesh/material names; original JSON has no _MaterialType",
                    @"loaded_control_fields":loaded,@"inferred_bindings_in_use":inferred,
                    @"read_source_scalar_fields":read_scalars,@"read_source_color_fields":read_colors};
            }
            if (![floats isKindOfClass:[NSDictionary class]] || ![colors isKindOfClass:[NSDictionary class]])
                throw std::runtime_error("ZZZ material requires source_floats and source_colors");
            // Optional reference-match edits are explicitly authored and reported separately.
            NSDictionary *authored_floats=entry[@"authored_floats"] ?: @{};
            NSDictionary *authored_colors=entry[@"authored_colors"] ?: @{};
            if (![authored_floats isKindOfClass:[NSDictionary class]] || ![authored_colors isKindOfClass:[NSDictionary class]])
                throw std::runtime_error("Invalid authored material overrides");
            if (source_report.count) {
                NSString *report_key=nil;
                for (NSString *k in source_report) if ([source_report[k][@"source"] isEqual:entry[@"source_material_json"]]) report_key=k;
                if (report_key) {
                    NSMutableDictionary *record=[source_report[report_key] mutableCopy];
                    record[@"authored_scalar_overrides"]=authored_floats;
                    record[@"authored_color_overrides"]=authored_colors;
                    source_report[report_key]=record;
                }
            }
            const auto scalar=[&](NSString *key,float fallback) {
                id value=authored_floats[key] ?: floats[key];if (!value) {
                    if (config.asset_toon) scalar_defaults[key]=@(fallback);
                    return fallback;
                }
                if (![value isKindOfClass:[NSNumber class]] || !std::isfinite([value floatValue]))
                    throw std::runtime_error("Invalid ZZZ scalar");
                if (config.asset_toon && !authored_floats[key] && ![read_scalars containsObject:key]) [read_scalars addObject:key];
                return [value floatValue];
            };
            const auto color=[&](NSString *key,F4 fallback) {
                id value=authored_colors[key] ?: colors[key];if (!value) {
                    if (config.asset_toon) color_defaults[key]=@[@(fallback.x),@(fallback.y),@(fallback.z),@(fallback.w)];
                    return fallback;
                }
                if (![value isKindOfClass:[NSDictionary class]]) throw std::runtime_error("Invalid ZZZ color");
                NSArray *channels=@[@"r",@"g",@"b",@"a"];
                for (unsigned c=0;c<4;++c) {
                    id x=value[channels[c]];
                    if (![x isKindOfClass:[NSNumber class]] || !std::isfinite([x floatValue]))
                        throw std::runtime_error("Invalid ZZZ color channel");
                    fallback[c]=[x floatValue];
                }
                if (config.asset_toon && !authored_colors[key] && ![read_colors containsObject:key]) [read_colors addObject:key];
                return fallback;
            };
            for (unsigned i=0;i<5;++i) {
                const auto key=[&](NSString *stem) {return i ? [stem stringByAppendingFormat:@"%u",i+1] : stem;};
                material.zzz_color[i]=color(key(@"_Color"),F4{1,1,1,1});
                material.zzz_shallow[i]=color(key(@"_ShallowColor"),F4{0.8f,0.8f,0.8f,1});
                material.zzz_shadow[i]=color(key(@"_ShadowColor"),F4{0.6f,0.6f,0.6f,1});
                material.zzz_specular[i]=color(key(@"_SpecularColor"),F4{1,1,1,1});
                material.zzz_outline[i]=color(key(@"_OutlineColor"),F4{1,1,1,1});
                material.zzz_response[i]={scalar(key(@"_AlbedoSmoothness"),scalar(@"_AlbedoSmoothness",0.05f)),scalar(key(@"_BrightMultiplier"),1),scalar(@"_AmbientColorScale",1),0};
                material.zzz_emission[i]=color(key(@"_EmissionColor"),F4{1,1,1,1});
                material.zzz_region[i]={scalar(key(@"_HighlightShape"),0),std::max(0.0001f,scalar(key(@"_ShapeSoftness"),0.1f)),
                    scalar(key(@"_SpecularRange"),1),scalar(key(@"_ToonSpecular"),0.01f)*scalar(key(@"_ModelSize"),1)};
            }
            material.zzz_params={scalar(@"_Metallic",0),scalar(@"_Glossiness",0.5f),scalar(@"_SpecIntensity",0.1f),scalar(@"_BumpScale",1)};
            material.zzz_source={scalar(@"_SkinMatId",0),scalar(@"_SpecularHighlights",1),scalar(@"_UseBumpMap",1),scalar(@"_UseMatCapMask",0)};
            material.zzz_emission_flags={scalar(@"_Emission",0),scalar(@"_SecondaryEmission",0),0,0};
            const auto selector=[&](NSString *key,unsigned fallback) {
                float v=scalar(key,float(fallback));
                if (v<0 || v>3 || v!=std::floor(v)) throw std::runtime_error("Invalid HoyoToon UV selector");
                return unsigned(v);
            };
            material.zzz_uv_rules={selector(@"_DoubleUV",1),unsigned(scalar(@"_SymmetryUV",0)!=0),
                unsigned(scalar(@"_LegacyOtherData",0)!=0),unsigned(scalar(@"_UseLegacyFace",0)!=0)};
            material.zzz_outline_rules={selector(@"_NormalUV",3),unsigned(scalar(@"_UseLightMapOL",0)!=0),
                unsigned(scalar(@"_DisableFOVScalingOL",0)!=0),unsigned(scalar(@"_OutlineZOff",0)!=0)};
            // User-authorized design, NOT recovered HoyoToon/game semantics.
            // Only read present asset values. Missing values leave the extension off
            // and must not be labelled as community-shader defaults in the report.
            const bool has_anisotropy=floats[@"_Anisotropy"]!=nil;
            const bool has_nose=floats[@"_NoseLineHoriDisp"]!=nil && floats[@"_NoseLineLkDnDisp"]!=nil;
            material.zzz_designed={has_anisotropy ? scalar(@"_Anisotropy",0) : 0,
                has_nose ? scalar(@"_NoseLineHoriDisp",0) : 0,
                has_nose ? scalar(@"_NoseLineLkDnDisp",0) : 0,
                float(unsigned(has_anisotropy)|unsigned(has_nose)*2u)};
            if(std::abs(material.zzz_designed.x)>1 || material.zzz_designed.y<0 || material.zzz_designed.y>1 ||
                material.zzz_designed.z<0 || material.zzz_designed.z>1)
                throw std::runtime_error("Invalid authored anisotropy/nose parameter");
            NSArray *post=@[@"_PostShallowTint",@"_PostShallowFadeTint",@"_PostShadowTint",@"_PostShadowFadeTint",@"_PostFrontTint",@"_PostSssTint"];
            const F4 post_defaults[6]={{0.956862748f,0.960784256f,0.9019608f,1},{0.8745098f,0.8f,0.7921569f,1},
                {0.8509804f,0.78039217f,0.772549033f,1},{0.929411769f,0.8509804f,0.8431372f,1},
                {1,0.996078432f,0.929411769f,1},{1,0.9481711f,0.929411769f,1}};
            for (unsigned i=0;i<6;++i) material.zzz_post[i]=color(post[i],post_defaults[i]);
            material.zzz_head=color(@"_HeadSphereNormalCenter",F4{0,0,0,0});
            const auto axis=[&](NSString *key,V3 fallback) {
                id a=entry[key];if (!a) return f4(fallback);
                if (![a isKindOfClass:[NSArray class]] || [(NSArray *)a count]!=3)
                    throw std::runtime_error("Head axis requires three components");
                V3 v{};for (unsigned i=0;i<3;++i) {
                    id x=a[i];if (![x isKindOfClass:[NSNumber class]] || !std::isfinite([x floatValue]))
                        throw std::runtime_error("Invalid head axis");
                    v[i]=[x floatValue];
                }
                if (simd_length_squared(v)<1e-12f) throw std::runtime_error("Zero head axis");
                return f4(normalized(v));
            };
            material.zzz_face_image=texture(@"face_lightmap");
            material.zzz_head_forward=axis(@"head_forward",V3{0,0,1});
            material.zzz_head_right=axis(@"head_right",V3{1,0,0});
            if (std::abs(simd_dot(material.zzz_head_forward.xyz,material.zzz_head_right.xyz))>0.001f)
                throw std::runtime_error("Head forward/right must be orthogonal");
            // HoyoToon shadow-pass tint, with a backend-specific soft visibility cone.
            material.zzz_face_shadow={1,0.9f,0.9f,0.015f};
            material.zzz_misc={scalar(@"_AlbedoSmoothness",0.05f),number(@"zzz_type",0),number(@"zzz_alpha",0),1};
            material.edge.x=scalar(@"_Outline",1);
            material.edge.y=scalar(@"_OutlineWidth",1)*0.0015f*config.outline_scale;
            if (material.zzz_misc.y==2 || material.zzz_misc.y==3) material.edge.x=0;
            material.surface.z=scalar(@"_BumpScale",1);
            const bool extended=[entry[@"zzz_extended"] boolValue];
            material.zzz_face_detail={scalar(@"_NoseSpecularScale",1),scalar(@"_NoseSmoothX",0),scalar(@"_NoseSmoothY",0.1f),extended ? 1.0f : 0.0f};
            if (material.zzz_face_detail.z<=material.zzz_face_detail.y)
                material.zzz_face_detail.z=material.zzz_face_detail.y+0.0001f;
            material.zzz_render={unsigned(number(@"opacity_mode",0)),unsigned(number(@"alpha_source",0)),
                unsigned(scalar(@"_Cull",0)),unsigned(number(@"eye_role",0))};
            material.zzz_alpha={scalar(@"_Cutoff",0.5f),scalar(@"_MinStencilAlpha",0),number(@"eye_reveal_depth",0.08f),scalar(@"_DoubleSided",0)};
            material.zzz_eye_lut=texture(@"eye_color_map");
            if (material.zzz_eye_lut.w && (material.zzz_eye_lut.y!=16 || material.zzz_eye_lut.z!=16))
                throw std::runtime_error("Eye LUT must be 16x16");
            if (number(@"opacity_mode",0)!=float(material.zzz_render.x) || material.zzz_render.x>2 ||
                number(@"alpha_source",0)!=float(material.zzz_render.y) || material.zzz_render.y>1 ||
                scalar(@"_Cull",0)!=float(material.zzz_render.z) || material.zzz_render.z>2 ||
                number(@"eye_role",0)!=float(material.zzz_render.w) || material.zzz_render.w>4 ||
                material.zzz_alpha.x<0 || material.zzz_alpha.x>1 || material.zzz_alpha.y<0 || material.zzz_alpha.y>1 ||
                material.zzz_alpha.z<=0 || material.zzz_alpha.z>1)
                throw std::runtime_error("Invalid ZZZ visibility parameter");
            id matcaps=entry[@"matcap_textures"];
            if (matcaps && (![matcaps isKindOfClass:[NSArray class]] || [(NSArray *)matcaps count]!=5))
                throw std::runtime_error("MatCap binding requires five paths/nulls");
            for (unsigned i=0;i<5;++i) {
                const auto key=[&](NSString *stem) {return i ? [stem stringByAppendingFormat:@"%u",i+1] : stem;};
                if ((!config.asset_toon || !config.strict_bindings) && matcaps && matcaps[i]!=[NSNull null]) {
                    if (![matcaps[i] isKindOfClass:[NSString class]]) throw std::runtime_error("Invalid MatCap texture path");
                    material.zzz_matcap_images[i]=image(directory/fs::path([(NSString *)matcaps[i] UTF8String]));
                }
                material.zzz_matcap_tint[i]=color(key(@"_MatCapColorTint"),F4{1,1,1,1});
                material.zzz_matcap_params[i]={scalar(key(@"_MatCapColorBurst"),1),scalar(key(@"_MatCapAlphaBurst"),1),
                    scalar(key(@"_MatCapBlendMode"),0),scalar(key(@"_MatCapTexID"),100)};
                material.zzz_matcap_motion[i]={scalar(key(@"_MatCapUSpeed"),0),scalar(key(@"_MatCapVSpeed"),0),
                    scalar(key(@"_MatCapRefract"),0),scalar(key(@"_RefractDepth"),0.5f)};
                material.zzz_matcap_refract[i]=color(key(@"_RefractParam"),F4{5,5,0,0});
                material.zzz_rim_colors[i]=color(key(@"_RimGlowLightColor"),F4{1,1,1,1});
                material.zzz_sun_colors[i]=color(key(@"_UISunColor"),F4{1,0.92f,0.9f,1});
                if (material.zzz_matcap_params[i].z<0 || material.zzz_matcap_params[i].z>2 ||
                    material.zzz_matcap_params[i].x<0 || material.zzz_matcap_params[i].y<0)
                    throw std::runtime_error("Invalid MatCap blend/burst");
            }
            material.zzz_effects={extended ? scalar(@"_MatCap",0) : 0,extended ? scalar(@"_RimGlow",0) : 0,
                scalar(@"_RimWidth",1),number(@"effect_time",0)};
            if (material.zzz_effects.z<0 || material.zzz_effects.z>100) throw std::runtime_error("Invalid rim width");
        }
    }
    std::uint32_t material(V3 color, unsigned kind=0, float parameter=0) {
        GPUMaterial m{};
        m.color=f4(color,parameter); m.uv_transform={1,1,0,0}; m.flags={kind,0,1,0};
        materials.push_back(m);
        return static_cast<std::uint32_t>(materials.size()-1);
    }
    void sphere(V3 center,float radius,std::uint32_t mat) {
        GPUPrimitive p{}; p.a=f4(center,radius); p.meta={0,mat,0,0}; primitives.push_back(p);
    }
    explicit HostScene(const Config &config) : profile_library(config.toon_profiles) {
        if (!config.no_ground) sphere({0,-1000,0},1000,material({config.ground_albedo,config.ground_albedo,config.ground_albedo}));
        if (!config.character_scene) {
        sphere({0,1,0},1,material({1,1,1},2,1.5f));
        sphere({-4,1,0},1,material({0.4f,0.2f,0.1f}));
        sphere({4,1,0},1,material({0.7f,0.6f,0.5f},1));
        Random rng;
        for (int a=-11;a<=10;++a) for (int b=-11;b<=10;++b) {
            const float choice=rng.next();
            float x=a+0.9f*rng.next(), z=b+0.9f*rng.next();
            V3 center{x,0.2f,z};
            if (simd_length(center-V3{4,0.2f,0})<0.9f) continue;
            std::uint32_t id;
            if (choice<0.8f) { V3 c1=rng.color(),c2=rng.color(); id=material(c1*c2); }
            else if (choice<0.95f) { V3 c=0.5f+0.5f*rng.color(); id=material(c,1,0.5f*rng.next()); }
            else id=material({1,1,1},2,1.5f);
            sphere(center,0.2f,id);
        }
        }
        if (!config.obj.empty()) {
            const auto mesh=assets::load_obj(config.obj);
            NSDictionary *mmd_entries=nil;
            fs::path mmd_directory;
            fs::path attribute_path;
            if (!config.mmd_materials.empty()) {
                const auto path=fs::absolute(config.mmd_materials);
                NSData *data=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.string().c_str()]];
                NSError *error=nil;
                id document=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:&error] : nil;
                if (![document isKindOfClass:[NSDictionary class]] || ![document[@"materials"] isKindOfClass:[NSDictionary class]])
                    throw std::runtime_error("Cannot read MMD material JSON: "+path.string());
                mmd_entries=document[@"materials"];mmd_directory=path.parent_path();
                id attributes=document[@"triangle_attributes"];
                if (attributes) {
                    if (![attributes isKindOfClass:[NSString class]]) throw std::runtime_error("Invalid attribute path");
                    attribute_path=mmd_directory/[(NSString *)attributes UTF8String];
                }
            }
            const std::uint32_t base=static_cast<std::uint32_t>(materials.size());
            for (const auto &s:mesh.materials) {
                GPUMaterial m{};
                m.color=f4(v3(s.diffuse));
                m.uv_transform={s.u_scale,s.v_scale,s.u_offset,s.v_offset};
                m.flags={0,unsigned(s.clamp),unsigned(s.srgb),0};
                if (!s.image_path.empty()) m.image=image(s.image_path);
                if (mmd_entries) {
                    id entry=mmd_entries[[NSString stringWithUTF8String:s.name.c_str()]];
                    if (entry) {
                        if (![entry isKindOfClass:[NSDictionary class]]) throw std::runtime_error("Invalid MMD material entry");
                        mmd_material(m,entry,mmd_directory,config);
                    } else if (s.name!="__default") throw std::runtime_error("Missing MMD material: "+s.name);
                }
                if (!config.asset_toon) profile_library.apply(m,s.name);
                materials.push_back(m);
            }
            // This PMX export has no verified HoyoToon UV3 direction encoding.
            // Author a separate smooth direction field, welding UV/normal seams
            // only within each material. Shading normals and base UV stay intact.
            using OutlineKey=std::tuple<unsigned,long long,long long,long long>;
            auto outline_key=[](unsigned material,V3 p) {
                return OutlineKey{material,std::llround(p.x*1000000),
                    std::llround(p.y*1000000),std::llround(p.z*1000000)};
            };
            std::map<OutlineKey,V3> smooth;
            for (const auto &t:mesh.triangles) {
                V3 a=v3(t.p[0]),b=v3(t.p[1]),c=v3(t.p[2]);
                V3 geometric=normalized(simd_cross(b-a,c-a));
                for (int i=0;i<3;++i) {
                    V3 point=v3(t.p[i]),e0=normalized(v3(t.p[(i+1)%3])-point),
                       e1=normalized(v3(t.p[(i+2)%3])-point);
                    float angle=std::acos(std::clamp(simd_dot(e0,e1),-1.0f,1.0f));
                    V3 normal=t.has_normals ? normalized(v3(t.n[i])) : geometric;
                    auto key=outline_key(t.material_id,point);
                    auto [it,inserted]=smooth.try_emplace(key,V3{0,0,0});
                    it->second+=normal*angle;
                }
            }
            std::vector<std::array<F4,12>> attributes;bool face_attributes=false,four_uv_attributes=false;
            if (!attribute_path.empty()) {
                std::ifstream file(attribute_path,std::ios::binary);char magic[8]={};std::uint32_t count=0;
                file.read(magic,8);file.read(reinterpret_cast<char *>(&count),4);
                four_uv_attributes=std::memcmp(magic,"ZZZATTR3",8)==0;
                face_attributes=four_uv_attributes || std::memcmp(magic,"ZZZATTR2",8)==0;
                if (!file || (!face_attributes && std::memcmp(magic,"ZZZATTR1",8)) || count!=mesh.triangles.size())
                    throw std::runtime_error("ZZZ attribute header/count mismatch");
                attributes.resize(count);
                if (four_uv_attributes) file.read(reinterpret_cast<char *>(attributes.data()),count*sizeof(attributes[0]));
                else if (face_attributes) {
                    std::vector<std::array<F4,9>> old(count);
                    file.read(reinterpret_cast<char *>(old.data()),count*sizeof(old[0]));
                    for (std::size_t i=0;i<count;++i) for (unsigned v=0;v<3;++v)
                        for (unsigned c=0;c<3;++c) attributes[i][v*4+c]=old[i][v*3+c];
                }
                else {
                    std::vector<std::array<F4,6>> old(count);
                    file.read(reinterpret_cast<char *>(old.data()),count*sizeof(old[0]));
                    for (std::size_t i=0;i<count;++i) for (unsigned v=0;v<3;++v) {
                        attributes[i][v*4]=old[i][v*2];attributes[i][v*4+1]=old[i][v*2+1];
                    }
                }
                if (!file || file.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Invalid ZZZ attribute file size");
                for (const auto &record:attributes) for (auto v:record) for (int c=0;c<4;++c)
                    if (!std::isfinite(v[c])) throw std::runtime_error("Non-finite ZZZ vertex attribute");
            }
            std::size_t triangle_index=0;
            for (const auto &s:mesh.triangles) {
                GPUPrimitive p{};
                p.a=f4(v3(s.p[0]));p.b=f4(v3(s.p[1]));p.c=f4(v3(s.p[2]));
                p.n0=f4(v3(s.n[0]));p.n1=f4(v3(s.n[1]));p.n2=f4(v3(s.n[2]));
                p.uv01={s.uv[0].u,s.uv[0].v,s.uv[1].u,s.uv[1].v};
                p.uv2={s.uv[2].u,s.uv[2].v,0,0};
                F4 *outlines[3]={&p.outline0,&p.outline1,&p.outline2};
                for (int i=0;i<3;++i) {
                    V3 n=smooth.at(outline_key(s.material_id,v3(s.p[i])));
                    if (simd_length_squared(n)<1e-12f) n=v3(s.n[i]);
                    *outlines[i]=f4(normalized(n),1.0f);
                }
                p.meta={1,base+s.material_id,unsigned(s.has_uv)|(unsigned(s.has_normals)<<1),0};
                if (!attributes.empty()) {
                    const auto &a=attributes[triangle_index];
                    p.tangent0=a[0];p.vertex_color0=a[1];p.tangent1=a[4];p.vertex_color1=a[5];p.tangent2=a[8];p.vertex_color2=a[9];
                    p.face0=a[2];p.face1=a[6];p.face2=a[10];
                    p.extra_uv0=a[3];p.extra_uv1=a[7];p.extra_uv2=a[11];
                    p.outline0.w=a[1].x;p.outline1.w=a[5].x;p.outline2.w=a[9].x;
                    p.meta.w=four_uv_attributes ? 7 : face_attributes ? 3 : 1;
                    if(config.asset_toon && four_uv_attributes) {
                        const auto &m=materials[p.meta.y];
                        for(unsigned v=0;v<3;++v) {
                            const F4 &t=a[v*4];const V3 n=v3(s.n[v]);
                            const unsigned selector=m.zzz_outline_rules.x;
                            const simd_float2 uv=selector==0 ? simd_float2{s.uv[v].u,s.uv[v].v} : selector==1 ? a[v*4+2].xy : selector==2 ? a[v*4+3].xy : a[v*4+3].zw;
                            const float z=std::sqrt(std::max(0.0f,1.0f-std::min(1.0f,simd_dot(uv,uv))));
                            V3 direction=uv.x*t.xyz+uv.y*t.w*simd_cross(n,t.xyz)+z*n;
                            *outlines[v]=f4(simd_length_squared(direction)>1e-12f ? normalized(direction) : n,a[v*4+1].x);
                            if(m.zzz_outline_rules.y) {
                                // vs_outline SampleLevel(linear_repeat, UV0, 0).B.
                                U4 image=m.zzz_misc.y==1 ? m.zzz_face_image : m.normal_image;
                                if(image.w) {
                                    const float x=s.uv[v].u*image.y-.5f,y=(1-s.uv[v].v)*image.z-.5f;
                                    const int ix=int(std::floor(x)),iy=int(std::floor(y));
                                    const float fx=x-ix,fy=y-iy;
                                    const auto texel=[&](int tx,int ty) {
                                        tx=(tx%int(image.y)+int(image.y))%int(image.y);ty=(ty%int(image.z)+int(image.z))%int(image.z);
                                        return float(pixels[image.x+4*(ty*image.y+tx)+2])/255;
                                    };
                                    outlines[v]->w*=((1-fx)*texel(ix,iy)+fx*texel(ix+1,iy))*(1-fy)+
                                        ((1-fx)*texel(ix,iy+1)+fx*texel(ix+1,iy+1))*fy;
                                }
                            }
                        }
                    }
                }
                ++triangle_index;primitives.push_back(p);
            }
            std::cout<<"Loaded OBJ: "<<mesh.triangles.size()<<" triangles, "<<mesh.materials.size()<<" materials\n";
        }
        sphere({0,5,0},1,material({4,4,4},3));
        for (unsigned i=0;i<config.area_lights.size();++i) {
            const GPUAreaLight &light=config.area_lights[i];
            const unsigned id=material(light.emission.xyz,3);materials[id].flags.w=i+1;
            const V3 c=light.center.xyz,u=light.u.xyz,v=light.v.xyz;
            const V3 corners[4]={c-u-v,c+u-v,c+u+v,c-u+v};
            for (const auto indices : {std::array<int,3>{0,1,2},std::array<int,3>{0,2,3}}) {
                GPUPrimitive p{};p.a=f4(corners[indices[0]]);p.b=f4(corners[indices[1]]);p.c=f4(corners[indices[2]]);
                V3 n=normalized(simd_cross(u,v));p.n0=p.n1=p.n2=f4(n);p.meta={1,id,2,0};primitives.push_back(p);
            }
        }
        // In a fixed-camera render the hull vertices never change. Bake the same
        // geometry once and build tight BVH bounds instead of rebuilding it at
        // every leaf visited by every path and shadow ray.
        if(config.asset_toon && config.headless && !config.no_outline && !config.dynamic_hulls) {
            const V3 origin=config.camera_set ? config.camera : config.character_scene ? V3{.90f,1.05f,3.75f} : V3{13,2,3};
            const V3 target=config.target_set ? config.target : config.character_scene ? V3{.03f,.87f,-.10f} : V3{0,0,0};
            const V3 back=normalized(origin-target),right=normalized(simd_cross(V3{0,1,0},back)),up=simd_cross(back,right);
            const auto vertex=[&](V3 position,F4 direction,const GPUMaterial &m) {
                const float fov=m.zzz_outline_rules.z ? 1.0f : 2.414f*std::tan(config.fov*3.1415926f/360);
                const V3 delta=position-origin;
                const float z=m.zzz_outline_rules.w ? simd_dot(direction.xyz,back) : -.0001f;
                const V3 normal{simd_dot(direction.xyz,right),simd_dot(direction.xyz,up),z};
                const V3 unit=normalized(normal);
                const float offset=(.001f+.009f*std::clamp(1+simd_dot(delta,back)*fov,0.0f,1.0f))*.01f;
                return position+delta*offset+m.edge.y*direction.w*(right*unit.x+up*unit.y+back*z);
            };
            const unsigned original_count=primitives.size();
            for(unsigned index=0;index<original_count;++index) {
                const GPUPrimitive source=primitives[index];const auto &m=materials[source.meta.y];
                if(source.meta.x!=1 || m.flags.x!=4 || m.mmd.w>0 || m.edge.x<=0 || m.edge.y<=0) continue;
                if(index>0xffffffu) throw std::runtime_error("Too many source triangles for hull linkage");
                GPUPrimitive hull=source;
                hull.a=f4(vertex(source.a.xyz,source.outline0,m));
                hull.b=f4(vertex(source.b.xyz,source.outline1,m));
                hull.c=f4(vertex(source.c.xyz,source.outline2,m));
                const bool negative=simd_dot(simd_cross(source.b.xyz-source.a.xyz,source.c.xyz-source.a.xyz),source.n0.xyz+source.n1.xyz+source.n2.xyz)<0;
                hull.meta.x=2;hull.meta.z|=negative ? 4u : 0u;hull.meta.w=(index<<8)|(source.meta.w&255u);
                primitives.push_back(hull);
            }
        }
        if (primitives.size()>(UINT32_MAX-1u)/2u) throw std::runtime_error("Too many primitives");
        order.resize(primitives.size()); std::iota(order.begin(),order.end(),0u);
        nodes.reserve(primitives.size()*2-1);
        build(0,order.size());
    }
    std::pair<V3,V3> bounds(std::uint32_t index) const {
        const auto &p=primitives[index];
        if (p.meta.x==0) return {p.a.xyz-p.a.w,p.a.xyz+p.a.w};
        return {minimum(p.a.xyz,minimum(p.b.xyz,p.c.xyz))-0.0001f,
                maximum(p.a.xyz,maximum(p.b.xyz,p.c.xyz))+0.0001f};
    }
    std::uint32_t build(std::size_t begin,std::size_t end) {
        const auto id=static_cast<std::uint32_t>(nodes.size());nodes.emplace_back();
        V3 lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
        for (auto i=begin;i<end;++i) {auto b=bounds(order[i]);lo=minimum(lo,b.first);hi=maximum(hi,b.second);}
        nodes[id].lo=f4(lo);nodes[id].hi=f4(hi);
        if (end-begin==1) nodes[id].link={0,0,order[begin],1};
        else {
            V3 extent=hi-lo;
            const int axis=extent.x>=extent.y && extent.x>=extent.z ? 0 : (extent.y>=extent.z ? 1 : 2);
            const auto middle=begin+(end-begin)/2;
            std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,
                [&](auto a,auto b){auto ba=bounds(a),bb=bounds(b);
                    return ba.first[axis]+ba.second[axis]<bb.first[axis]+bb.second[axis];});
            const auto left=build(begin,middle),right=build(middle,end);
            nodes[id].link={left,right,0,0};
        }
        return id;
    }
};

class Renderer {
public:
    Config config;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> trace, bilateral, display, guide, depth_guide;
    id<MTLBuffer> primitive_buffer,node_buffer,material_buffer,pixel_buffer,rng_buffer;
    id<MTLBuffer> frame_buffer,accum_buffer,filtered_buffer,bgra_buffer,guide_buffer,guide_material_buffer,depth_buffer;
    V3 position{13,2,3}, target{0,0,0};
    bool paused=false,dirty=false,right_button=false,middle_button=false,quit=false;
    bool zzz_scene=false,face_sdf_scene=false,extended_scene=false,matcap_scene=false;
    NSDictionary *profile_report=nil;
    NSDictionary *source_report=nil;
    NSArray *texture_report=nil;
    bool keys[6]{};
    unsigned frames=0,accumulated=0;
    float outline_padding=0.002f;
    Renderer(Config c,const fs::path &library_path):config(std::move(c)) {
        device=MTLCreateSystemDefaultDevice();
        if (!device) throw std::runtime_error("No Metal device available");
        queue=[device newCommandQueue];
        if (!queue) throw std::runtime_error("Cannot create Metal command queue");
        NSError *error=nil;
        id<MTLLibrary> library=nil;
        std::uint64_t shader_key=14695981039346656037ULL;
        const auto hash_bytes=[&](const void *data,std::size_t count) {
            const auto *bytes=static_cast<const unsigned char *>(data);
            for (std::size_t i=0;i<count;++i) {shader_key^=bytes[i];shader_key*=1099511628211ULL;}
        };
        NSString *cache_target=[device.name stringByAppendingString:NSProcessInfo.processInfo.operatingSystemVersionString];
        hash_bytes(cache_target.UTF8String,std::strlen(cache_target.UTF8String));
        if (fs::exists(library_path)) {
            NSData *bytes=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:library_path.c_str()]];
            hash_bytes(bytes.bytes,bytes.length);
            library=[device newLibraryWithURL:[NSURL fileURLWithPath:
                [NSString stringWithUTF8String:library_path.string().c_str()]] error:&error];
        } else {
            // macOS can compile MSL through Metal even when Xcode's offline component is absent.
            const auto directory=library_path.parent_path();
            NSString *source=[NSString stringWithContentsOfFile:[NSString stringWithUTF8String:
                (directory/"pathtracer.metal").string().c_str()] encoding:NSUTF8StringEncoding error:&error];
            NSString *types=[NSString stringWithContentsOfFile:[NSString stringWithUTF8String:
                (directory/"shared_types.h").string().c_str()] encoding:NSUTF8StringEncoding error:&error];
            if (!source || !types) throw std::runtime_error("Missing Metal source beside executable");
            source=[source stringByReplacingOccurrencesOfString:@"#include \"shared_types.h\"" withString:types];
            hash_bytes(source.UTF8String,std::strlen(source.UTF8String));
            MTLCompileOptions *options=[MTLCompileOptions new];
            options.fastMathEnabled=NO;
            options.languageVersion=MTLLanguageVersion3_0;
            std::cout<<"Loading Metal shader library..."<<std::endl;
            library=[device newLibraryWithSource:source options:options error:&error];
        }
        if (!library) throw std::runtime_error("Cannot compile/load Metal shaders: "+
            std::string(error ? error.localizedDescription.UTF8String : "unknown error"));
        // The source/device/OS key prevents stale shader binaries or ABI reuse.
        NSString *cache_name=[NSString stringWithFormat:@"pathtracer-%016llx.metalar",(unsigned long long)shader_key];
        fs::path cache_path=library_path.parent_path()/cache_name.UTF8String;
        NSURL *cache_url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:cache_path.c_str()]];
        MTLBinaryArchiveDescriptor *archive_desc=[MTLBinaryArchiveDescriptor new];
        if (fs::exists(cache_path)) archive_desc.url=cache_url;
        NSError *archive_error=nil;
        id<MTLBinaryArchive> archive=[device newBinaryArchiveWithDescriptor:archive_desc error:&archive_error];
        if (!archive && archive_desc.url) {archive_desc.url=nil;archive=[device newBinaryArchiveWithDescriptor:archive_desc error:&archive_error];}
        bool archive_dirty=false;
        const auto pipeline=[&](NSString *name) -> id<MTLComputePipelineState> {
            id<MTLFunction> function=[library newFunctionWithName:name];
            if (!function) throw std::runtime_error("Missing Metal function");
            NSError *pipeline_error=nil;
            MTLComputePipelineDescriptor *descriptor=[MTLComputePipelineDescriptor new];
            descriptor.computeFunction=function;
            if (archive) descriptor.binaryArchives=@[archive];
            id<MTLComputePipelineState> result=nil;
            if (archive) result=[device newComputePipelineStateWithDescriptor:descriptor
                options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:&pipeline_error];
            if (!result) {
                std::cout<<"Compiling Metal pipeline "<<name.UTF8String<<" (cache miss)..."<<std::endl;
                if (archive && [archive addComputePipelineFunctionsWithDescriptor:descriptor error:&archive_error]) archive_dirty=true;
                result=[device newComputePipelineStateWithDescriptor:descriptor options:MTLPipelineOptionNone reflection:nil error:&pipeline_error];
            }
            if (!result) throw std::runtime_error("Cannot create compute pipeline: "+std::string(pipeline_error.localizedDescription.UTF8String));
            return result;
        };
        trace=pipeline(@"render_kernel");bilateral=pipeline(@"bilateral_kernel");display=pipeline(@"display_kernel");guide=pipeline(@"guide_kernel");depth_guide=pipeline(@"depth_kernel");
        if (archive && archive_dirty && ![archive serializeToURL:cache_url error:&archive_error])
            std::cerr<<"Metal pipeline cache could not be saved; rendering continues.\n";
        if (config.check) return;
        const HostScene scene(config);
        profile_report=[scene.profile_library.report copy];
        source_report=[scene.source_report copy];
        NSMutableArray *textures=[NSMutableArray array];
        for (const auto &[path,descriptor]:scene.images) [textures addObject:@{
            @"file":[NSString stringWithUTF8String:path.c_str()],@"width":@(descriptor.y),@"height":@(descriptor.z)}];
        texture_report=[textures copy];
        for (const auto &m:scene.materials) {
            if (m.edge.x>0) outline_padding=std::max(outline_padding,m.edge.y+0.002f);
            zzz_scene|=m.zzz_misc.w>0;
            extended_scene|=m.zzz_face_detail.w>0;
            for (unsigned slot=0;slot<5;++slot) matcap_scene|=m.zzz_effects.x>0 && m.zzz_matcap_images[slot].w>0;
            face_sdf_scene|=m.zzz_misc.w>0 && m.zzz_misc.y==1 && m.zzz_face_image.w>0;
        }
        primitive_buffer=buffer(scene.primitives.data(),scene.primitives.size()*sizeof(GPUPrimitive),@"primitives");
        node_buffer=buffer(scene.nodes.data(),scene.nodes.size()*sizeof(GPUNode),@"BVH");
        material_buffer=buffer(scene.materials.data(),scene.materials.size()*sizeof(GPUMaterial),@"materials");
        pixel_buffer=buffer(scene.pixels.data(),scene.pixels.size(),@"base-color pixels");
        const std::size_t count=std::size_t(config.width)*config.height;
        rng_buffer=buffer(nullptr,count*sizeof(GPURandom),@"RNG states");
        frame_buffer=buffer(nullptr,count*sizeof(F4),@"linear frame");
        accum_buffer=buffer(nullptr,count*sizeof(F4),@"accumulation");
        filtered_buffer=buffer(nullptr,count*sizeof(F4),@"bilateral output");
        bgra_buffer=buffer(nullptr,count*4,@"display BGRA");
        depth_buffer=buffer(nullptr,count*sizeof(float),@"linear camera depth");
        guide_buffer=buffer(nullptr,count*sizeof(F4),@"outline depth guide");
        guide_material_buffer=buffer(nullptr,count*sizeof(unsigned),@"outline material guide");
        paused=config.headless;
        if (config.character_scene) {
            position={0.90f,1.05f,3.75f};target={0.03f,0.87f,-0.10f};
        }
        if (config.camera_set) position=config.camera;
        if (config.target_set) target=config.target;
        std::cout<<"Metal device: "<<device.name.UTF8String<<", "<<scene.primitives.size()
                 <<" primitives, "<<scene.nodes.size()<<" BVH nodes\n";
    }
    id<MTLBuffer> buffer(const void *data,std::size_t length,NSString *label) {
        if (length==0 || length>device.maxBufferLength) throw std::runtime_error("Buffer size unsupported by Metal device");
        id<MTLBuffer> result=data ? [device newBufferWithBytes:data length:length options:MTLResourceStorageModeShared]
                                 : [device newBufferWithLength:length options:MTLResourceStorageModeShared];
        if (!result) throw std::runtime_error("Cannot allocate Metal buffer");
        result.label=label;return result;
    }
    void basis(V3 &forward,V3 &right,V3 &up) const {
        forward=normalized(target-position);right=normalized(simd_cross(forward,V3{0,1,0}));up=simd_cross(right,forward);
    }
    static V3 rotate(V3 v,V3 k,float angle) {
        k=normalized(k);return v*std::cos(angle)+simd_cross(k,v)*std::sin(angle)+k*simd_dot(k,v)*(1-std::cos(angle));
    }
    static V3 collision(V3 p) {
        const V3 centers[]={{0,-1000,0},{0,1,0},{-4,1,0},{4,1,0}};
        const float radii[]={1000,1,1,1};
        for (int i=0;i<4;++i) {V3 d=p-centers[i];float length=simd_length(d),limit=radii[i]+0.2f;
            if (length<limit) p=centers[i]+(length<1e-6f ? V3{0,limit,0} : d*(limit/length));}
        return p;
    }
    void look(float dx,float dy) {
        V3 f,r,u;basis(f,r,u);float distance=simd_length(target-position);
        f=rotate(f,{0,1,0},dx*0.002f);
        float pitch=std::asin(std::clamp(f.y,-1.0f,1.0f));
        const float limit=89.0f*3.14159265358979323846f/180;
        float next=std::clamp(pitch+dy*0.0015f,-limit,limit);
        f=rotate(f,r,next-pitch);target=position+f*distance;dirty=true;
    }
    void pan(float dx,float dy) {
        V3 f,r,u;basis(f,r,u);V3 offset=(r*(-dx)+u*dy)*0.001f*simd_length(target-position);
        position=collision(position+offset);target+=offset;dirty=true;
    }
    void dolly(float notches) {
        V3 f,r,u;basis(f,r,u);V3 offset=f*notches*0.1f*simd_length(target-position);
        position=collision(position+offset);target+=offset;dirty=true;
    }
    void fly(float dt) {
        if (!right_button) return;
        V3 f,r,u;basis(f,r,u);
        V3 offset=(r*(float(keys[3])-float(keys[1]))+u*(float(keys[5])-float(keys[4]))
                  +f*(float(keys[0])-float(keys[2])))*2.0f*dt;
        if (simd_length_squared(offset)==0) return;
        position=collision(position+offset);target+=offset;dirty=true;
    }
    GPUParams params() const {
        GPUParams p{};
        V3 w=normalized(position-target),u=normalized(simd_cross(V3{0,1,0},w)),v=simd_cross(w,u);
        const float half_height=std::tan(config.fov*3.1415926f/360);
        const float half_width=float(config.width)/config.height*half_height;
        p.origin=f4(position,config.aperture/2);
        p.lower_left=f4(position-half_width*config.focus*u-half_height*config.focus*v-config.focus*w);
        p.horizontal=f4(2*half_width*config.focus*u);p.vertical=f4(2*half_height*config.focus*v);
        p.right=f4(u);p.up=f4(v);
        p.image={unsigned(config.width),unsigned(config.height),unsigned(config.samples),unsigned(config.depth)};
        p.counts={unsigned(primitive_buffer.length/sizeof(GPUPrimitive)),unsigned(node_buffer.length/sizeof(GPUNode)),
                  paused ? accumulated+1 : 0,frames};
        const unsigned mode=config.mmd_materials.empty() || config.legacy_character ? 0u : config.physical_character ? 1u : 2u;
        p.settings={unsigned(paused),unsigned(config.sigma>0),unsigned(!config.mmd_materials.empty()),
                    config.no_hair_highlight ? 2u : mode!=0 ? 1u : 0u};
        p.filter={2,config.sigma,paused ? 1.0f/(accumulated+1) : 1.0f,outline_padding};
        p.toon_light=f4(config.toon_light,config.toon_light_set ? 1 : 0);
        unsigned indirect_bits;std::memcpy(&indirect_bits,&config.indirect_strength,sizeof(indirect_bits));
        p.extensions={unsigned(extended_scene),indirect_bits,mode,unsigned(config.no_toon)};
        p.features={config.no_outline ? 0u : mode==0 ? 1u : config.asset_toon ? 2u | (config.headless && !config.dynamic_hulls ? 4u : 0u) : 0u,unsigned(!config.no_normal),unsigned(!config.no_pbr),config.zzz_comparison};
        p.lighting={config.sun_intensity,mode || config.environment_set ? config.environment_strength : 1.0f,config.exposure,config.sphere_intensity};
        p.rendering={unsigned(config.area_lights.size()),unsigned(config.tonemap && !config.legacy_character),
                     unsigned(!config.uniform_toon && !config.asset_toon),unsigned(config.asset_toon) | (unsigned(config.neutral_sky)<<1)};
        // Use an existing reserved component; preserve shared header and buffer ABI.
        const double half_angle=config.sun_angle*3.14159265358979323846/360;
        const float solid_angle=float(4*3.14159265358979323846*std::sin(half_angle)*std::sin(half_angle));
        p.tone={config.tone_knee,0,0,solid_angle};
        for (unsigned i=0;i<config.area_lights.size();++i) p.area_lights[i]=config.area_lights[i];
        return p;
    }
    void dispatch(id<MTLComputeCommandEncoder> encoder,id<MTLComputePipelineState> pipeline) {
        [encoder setComputePipelineState:pipeline];
        const NSUInteger h=std::max<NSUInteger>(1,std::min<NSUInteger>(8,pipeline.maxTotalThreadsPerThreadgroup/8));
        [encoder dispatchThreads:MTLSizeMake(config.width,config.height,1) threadsPerThreadgroup:MTLSizeMake(8,h,1)];
    }
    void render() {
        const bool update_guides=dirty || !paused || accumulated==0;
        if (dirty) {accumulated=0;dirty=false;}
        if (!paused) accumulated=0;
        GPUParams p=params();
        id<MTLCommandBuffer> command=[queue commandBuffer];
        if (paused && accumulated==0) {
            id<MTLBlitCommandEncoder> clear=[command blitCommandEncoder];
            [clear fillBuffer:accum_buffer range:NSMakeRange(0,accum_buffer.length) value:0];[clear endEncoding];
        }
        if (update_guides && p.extensions.z==0 && extended_scene && !config.no_toon && !(config.zzz_comparison&8192)) {
            id<MTLComputeCommandEncoder> depth_encoder=[command computeCommandEncoder];
            [depth_encoder setBuffer:primitive_buffer offset:0 atIndex:0];[depth_encoder setBuffer:node_buffer offset:0 atIndex:1];
            [depth_encoder setBuffer:material_buffer offset:0 atIndex:2];[depth_encoder setBuffer:pixel_buffer offset:0 atIndex:3];
            [depth_encoder setBytes:&p length:sizeof(p) atIndex:5];[depth_encoder setBuffer:depth_buffer offset:0 atIndex:10];
            dispatch(depth_encoder,depth_guide);[depth_encoder endEncoding];
        }
        // Finish clearing/prepasses before tracing. Small command buffers keep the
        // GPU responsive; global pixel indices and RNG states remain unchanged.
        [command commit];[command waitUntilCompleted];
        if (command.status==MTLCommandBufferStatusError)
            throw std::runtime_error("Metal preparation failed: "+std::string(command.error.localizedDescription.UTF8String));
        const auto tile_begin=std::chrono::steady_clock::now();
        auto last_tile_progress=tile_begin;
        const unsigned tile_columns=std::min(unsigned(config.width),std::min(128u,unsigned(config.trace_tile_pixels)));
        const unsigned tile_rows=std::max(1u,unsigned(config.trace_tile_pixels)/tile_columns);
        for (unsigned row=0;row<unsigned(config.height);row+=tile_rows) {
          for (unsigned column=0;column<unsigned(config.width);column+=tile_columns) {
            @autoreleasepool {
                GPUParams tile=p;
                // tone.y is reserved for the render kernel's bitwise row offset.
                float row_bits,column_bits;std::memcpy(&row_bits,&row,sizeof(row));std::memcpy(&column_bits,&column,sizeof(column));tile.tone.y=row_bits;tile.tone.z=column_bits;
                id<MTLCommandBuffer> tile_command=[queue commandBuffer];
                id<MTLComputeCommandEncoder> tile_encoder=[tile_command computeCommandEncoder];
                [tile_encoder setBuffer:primitive_buffer offset:0 atIndex:0];[tile_encoder setBuffer:node_buffer offset:0 atIndex:1];
                [tile_encoder setBuffer:material_buffer offset:0 atIndex:2];[tile_encoder setBuffer:pixel_buffer offset:0 atIndex:3];
                [tile_encoder setBuffer:rng_buffer offset:0 atIndex:4];[tile_encoder setBytes:&tile length:sizeof(tile) atIndex:5];
                [tile_encoder setBuffer:frame_buffer offset:0 atIndex:6];[tile_encoder setBuffer:accum_buffer offset:0 atIndex:7];
                [tile_encoder setBuffer:depth_buffer offset:0 atIndex:10];
                [tile_encoder setComputePipelineState:trace];
                const NSUInteger h=std::max<NSUInteger>(1,std::min<NSUInteger>(8,trace.maxTotalThreadsPerThreadgroup/8));
                [tile_encoder dispatchThreads:MTLSizeMake(std::min(tile_columns,unsigned(config.width)-column),std::min(tile_rows,unsigned(config.height)-row),1)
                    threadsPerThreadgroup:MTLSizeMake(8,h,1)];
                [tile_encoder endEncoding];[tile_command commit];[tile_command waitUntilCompleted];
                if (tile_command.status==MTLCommandBufferStatusError)
                    throw std::runtime_error("Metal tracing failed: "+std::string(tile_command.error.localizedDescription.UTF8String));
            }
          }
            const auto now=std::chrono::steady_clock::now();
            if(config.headless && std::chrono::duration<double>(now-last_tile_progress).count()>=15) {
                std::cout<<"Pass "<<(accumulated+1)<<", traced rows "<<std::min(row+tile_rows,unsigned(config.height))<<"/"<<config.height<<std::endl;
                last_tile_progress=now;
            }
        }
        command=[queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder;
        if (update_guides && (p.features.x&1u) && !config.mmd_materials.empty()) {
            encoder=[command computeCommandEncoder];
            [encoder setBuffer:primitive_buffer offset:0 atIndex:0];[encoder setBuffer:node_buffer offset:0 atIndex:1];
            [encoder setBuffer:material_buffer offset:0 atIndex:2];[encoder setBuffer:pixel_buffer offset:0 atIndex:3];
            [encoder setBytes:&p length:sizeof(p) atIndex:5];
            [encoder setBuffer:guide_buffer offset:0 atIndex:8];[encoder setBuffer:guide_material_buffer offset:0 atIndex:9];
            dispatch(encoder,guide);[encoder endEncoding];
        }
        id<MTLBuffer> source=paused ? accum_buffer : frame_buffer;
        if (config.sigma>0) {
            encoder=[command computeCommandEncoder];
            [encoder setBuffer:source offset:0 atIndex:0];[encoder setBuffer:filtered_buffer offset:0 atIndex:1];
            [encoder setBytes:&p length:sizeof(p) atIndex:2];dispatch(encoder,bilateral);[encoder endEncoding];
            source=filtered_buffer;
        }
        encoder=[command computeCommandEncoder];
        [encoder setBuffer:source offset:0 atIndex:0];[encoder setBuffer:bgra_buffer offset:0 atIndex:1];
        [encoder setBytes:&p length:sizeof(p) atIndex:2];
        [encoder setBuffer:guide_buffer offset:0 atIndex:3];[encoder setBuffer:material_buffer offset:0 atIndex:4];
        [encoder setBuffer:guide_material_buffer offset:0 atIndex:5];
        dispatch(encoder,display);[encoder endEncoding];
        [command commit];[command waitUntilCompleted];
        if (command.status==MTLCommandBufferStatusError)
            throw std::runtime_error("Metal execution failed: "+std::string(command.error.localizedDescription.UTF8String));
        if (paused) ++accumulated;
        ++frames;
    }
    CGImageRef image() const {
        CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGDataProviderRef provider=CGDataProviderCreateWithData(nullptr,bgra_buffer.contents,bgra_buffer.length,nullptr);
        CGImageRef image=CGImageCreate(config.width,config.height,8,32,std::size_t(config.width)*4,space,
            kCGBitmapByteOrder32Little|kCGImageAlphaPremultipliedFirst,provider,nullptr,false,kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);CGColorSpaceRelease(space);
        if (!image) throw std::runtime_error("Cannot create display image");
        return image;
    }
    void save(const std::string &filename) const {
        if (frames==0) return;
        const fs::path path=fs::absolute(filename);
        if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
        CGImageRef source=image();
        NSURL *url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.string().c_str()]];
        CGImageDestinationRef destination=CGImageDestinationCreateWithURL((__bridge CFURLRef)url,CFSTR("public.png"),1,nullptr);
        if (!destination) {CGImageRelease(source);throw std::runtime_error("Cannot create PNG output");}
        CGImageDestinationAddImage(destination,source,nullptr);
        const bool ok=CGImageDestinationFinalize(destination);
        CFRelease(destination);CGImageRelease(source);
        if (!ok) throw std::runtime_error("Cannot save PNG output");
        std::cout<<"Saved "<<path<<" ("<<accumulated*config.samples<<" accumulated spp)\n";
    }
};

@interface RenderView : NSView
@property(nonatomic,assign) Renderer *renderer;
@property(nonatomic) NSPoint lastPoint;
@end
@implementation RenderView
- (BOOL)acceptsFirstResponder {return YES;}
- (BOOL)isFlipped {return YES;}
- (void)drawRect:(NSRect)rect {
    [[NSColor blackColor] setFill];NSRectFill(self.bounds);
    Renderer *r=self.renderer;if (!r || r->frames==0) return;
    CGImageRef cg=r->image();NSImage *image=[[NSImage alloc] initWithCGImage:cg size:NSMakeSize(r->config.width,r->config.height)];
    CGImageRelease(cg);
    CGFloat scale=std::min(self.bounds.size.width/r->config.width,self.bounds.size.height/r->config.height);
    NSSize size=NSMakeSize(r->config.width*scale,r->config.height*scale);
    NSRect dst=NSMakeRect((self.bounds.size.width-size.width)/2,(self.bounds.size.height-size.height)/2,size.width,size.height);
    [image drawInRect:dst fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1 respectFlipped:YES hints:nil];
}
- (void)rightMouseDown:(NSEvent *)event {self.renderer->right_button=true;self.lastPoint=[self convertPoint:event.locationInWindow fromView:nil];}
- (void)rightMouseUp:(NSEvent *)event {self.renderer->right_button=false;}
- (void)rightMouseDragged:(NSEvent *)event {
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    self.renderer->look(p.x-self.lastPoint.x,p.y-self.lastPoint.y);self.lastPoint=p;
}
- (void)otherMouseDown:(NSEvent *)event {
    if (event.buttonNumber==2) {self.renderer->middle_button=true;self.lastPoint=[self convertPoint:event.locationInWindow fromView:nil];}
}
- (void)otherMouseUp:(NSEvent *)event {if (event.buttonNumber==2) self.renderer->middle_button=false;}
- (void)otherMouseDragged:(NSEvent *)event {
    if (event.buttonNumber!=2) return;
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    self.renderer->pan(p.x-self.lastPoint.x,p.y-self.lastPoint.y);self.lastPoint=p;
}
- (void)scrollWheel:(NSEvent *)event {self.renderer->dolly(event.scrollingDeltaY*(event.hasPreciseScrollingDeltas ? 0.1f : 1.0f));}
- (void)keyDown:(NSEvent *)event {
    if (event.keyCode==53) {self.renderer->quit=true;return;}
    NSString *text=event.charactersIgnoringModifiers.lowercaseString;if (!text.length) return;
    unichar ch=[text characterAtIndex:0];
    const char *keys="wasdqe";
    for (int i=0;i<6;++i) if (ch==keys[i]) self.renderer->keys[i]=true;
    if (ch=='f') {self.renderer->target={0,0,0};self.renderer->dirty=true;}
}
- (void)keyUp:(NSEvent *)event {
    NSString *text=event.charactersIgnoringModifiers.lowercaseString;if (!text.length) return;
    unichar ch=[text characterAtIndex:0];const char *keys="wasdqe";
    for (int i=0;i<6;++i) if (ch==keys[i]) self.renderer->keys[i]=false;
    if (ch==' ') {self.renderer->paused=!self.renderer->paused;self.renderer->accumulated=0;}
    if (ch=='p') {
        try {self.renderer->save(self.renderer->config.output.empty() ? "metal-render.png" : self.renderer->config.output);}
        catch (const std::exception &e) {std::cerr<<e.what()<<'\n';}
    }
}
@end
@interface WindowDelegate : NSObject<NSWindowDelegate>
@property(nonatomic,assign) Renderer *renderer;
@end
@implementation WindowDelegate
- (void)windowWillClose:(NSNotification *)notification {self.renderer->quit=true;}
- (void)windowDidResignKey:(NSNotification *)notification {
    std::fill(std::begin(self.renderer->keys),std::end(self.renderer->keys),false);
    self.renderer->right_button=self.renderer->middle_button=false;
}
@end

int main(int argc,char **argv)
{
    @autoreleasepool {
        try {
            const Config config=parse_args(argc,argv);
            const fs::path executable=fs::absolute(argv[0]);
            Renderer renderer(config,executable.parent_path()/"pathtracer.metallib");
            if (config.check) {
                std::cout<<"Metal pipelines compiled successfully on "<<renderer.device.name.UTF8String<<"\n";
                return 0;
            }
            NSWindow *window=nil;RenderView *view=nil;WindowDelegate *delegate=nil;
            if (!config.headless) {
                [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,config.width,config.height)
                    styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered defer:NO];
                window.releasedWhenClosed=NO;
                view=[[RenderView alloc] initWithFrame:NSMakeRect(0,0,config.width,config.height)];
                view.renderer=&renderer;view.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
                delegate=[WindowDelegate new];delegate.renderer=&renderer;window.delegate=delegate;
                window.contentView=view;[window center];[window makeKeyAndOrderFront:nil];[window makeFirstResponder:view];
                [NSApp finishLaunching];[NSApp activateIgnoringOtherApps:YES];
            }
            const auto start=std::chrono::steady_clock::now();
            float last=0,report=0;
            while (!renderer.quit) {
                @autoreleasepool {
                    if (window) {
                        NSEvent *event;
                        while ((event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode dequeue:YES])) [NSApp sendEvent:event];
                    }
                    const float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
                    if (renderer.quit || (config.seconds>0 && elapsed>=config.seconds) ||
                        (config.frames>0 && renderer.frames>=unsigned(config.frames))) break;
                    renderer.fly(elapsed-last);last=elapsed;renderer.render();
                    if (view) {[view setNeedsDisplay:YES];[view displayIfNeeded];[NSApp updateWindows];}
                    if (elapsed-report>=1) {
                        report=elapsed;
                        std::cout<<"t="<<elapsed<<"s frames="<<renderer.frames<<" accum="<<renderer.accumulated<<std::endl;
                        if (window) window.title=[NSString stringWithFormat:
                            @"Metal Ray Tracer - %.1f FPS - %d spp - ACC x%u - RMB/MMB/wheel/WASD/QE - SPACE accumulate - P save",
                            renderer.frames/std::max(elapsed,0.001f),config.samples,renderer.accumulated];
                    }
                }
            }
            const double render_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            if (!config.output.empty()) renderer.save(config.output);
            if (config.headless && !config.output.empty()) {
                const bool has_character=!config.mmd_materials.empty();
                const bool legacy=config.legacy_character && has_character;
                const bool toon_path=has_character && !legacy && !config.physical_character && !config.no_toon;
                NSMutableArray *area_report=[NSMutableArray array];
                for (const auto &l:config.area_lights) [area_report addObject:@{
                    @"center":@[@(l.center.x),@(l.center.y),@(l.center.z)],
                    @"half_u":@[@(l.u.x),@(l.u.y),@(l.u.z)],@"half_v":@[@(l.v.x),@(l.v.y),@(l.v.z)],
                    @"area":@(l.center.w),@"radiance":@[@(l.emission.x),@(l.emission.y),@(l.emission.z)]}];
                NSDictionary *report=@{
                    @"backend": @"Metal",
                    @"tone_mapping": config.tonemap && !legacy ? @"Hue-preserving exponential shoulder" : @"Legacy clipping",
                    @"tone_knee": @(config.tone_knee), @"rectangular_lights":area_report,
                    @"typed_toon_profiles": @(toon_path && !config.uniform_toon && !config.asset_toon),
                    @"asset_source_materials": @(config.asset_toon), @"asset_source_report":renderer.source_report,
                    @"loaded_texture_files":renderer.texture_report,
                    @"neutral_sky":@(config.neutral_sky),
                    @"sun_angular_radius_degrees":@(config.sun_angle), @"ground_linear_albedo":@(config.ground_albedo), @"ground_enabled":@(!config.no_ground),
                    @"sun_sampling":@"Finite cone: integrated radiance held constant, cone NEE and BSDF-hit MIS; zero angle retains delta emitter",
                    @"path_outline_scattering":config.asset_toon ? @"Thin ink shell: source angular colour, reflection and transmission f=R/(2*pi*abs(cos)); uniform sphere PDF; every bounce and shadow remains traced. Local adaptation, not official shader." : @"disabled",
                    @"source_path_adapter": config.asset_toon ? @"HoyoToon angular response / (2*pi*cos); uniform hemisphere PDF 1/(2*pi). Mathematical adapter, not recovered game BRDF." : @"disabled",
                    @"hoyotoon_reference": config.asset_toon ? @"Hoyotoon/HoyoToon d9e5ca2f312bf16fba89dee67d32c08b482dcda4; ZZZ common/program/shader" : @"disabled",
                    @"hoyotoon_uv_rules": config.asset_toon ? @"Raw UV0..3 selectors; source UV tangent-space outline decoding for ZZZATTR3; legacy packing and legacy face branches" : @"prepared package",
                    @"authored_hair_anisotropy": config.asset_toon ? @{
                        @"enabled":@(!(config.zzz_comparison&524288) && !(config.zzz_comparison&4)),
                        @"parameter":@"Original _Anisotropy; hair only with valid source tangent",
                        @"rule":@"Stretch HoyoToon highlight half-vector coordinates: tangent / 2^(2*A), bitangent * 2^(2*A). A=0 preserves original; not recovered game formula."} : @{},
                    @"authored_dynamic_nose": config.asset_toon ? @{
                        @"enabled":@(!(config.zzz_comparison&1048576) && !(config.zzz_comparison&512)),
                        @"parameters":@"Original _NoseLineHoriDisp / _NoseLineLkDnDisp",
                        @"rule":@"Head-local yaw and downward cosine gates; smoothstep half-width .05. Multiplies original D.a nose mask at every hit; not recovered game scripting."} : @{},
                    @"texture_binding_policy":config.asset_toon ? (config.strict_bindings ? @"Confirmed-only controls" : @"All prepared bindings used; inferred N/M/A, SDF and candidate MatCap recorded separately") : @"prepared package",
                    @"strict_texture_bindings":@(config.strict_bindings),
                    @"toon_profile_file": [NSString stringWithUTF8String:config.toon_profiles.c_str()],
                    @"toon_profile_bindings": renderer.profile_report,
                    @"character_transport": config.asset_toon ? @"Full path integration: source angular shading at every hit; original parameters and explicit authored overrides recorded separately; no camera overlays" : !has_character ? @"Standard material path tracing" : config.physical_character || (!legacy && config.no_toon) ? @"Full diffuse/GGX path transport, without camera-facing Toon overlays" : legacy ? @"Legacy Toon plus indirect correction" : @"Full path integration: banded diffuse and compact anisotropic specular BSDF at every character hit",
                    @"legacy_indirect_strength": @(legacy ? config.indirect_strength : 0),
                    @"sun_intensity": @(config.sun_intensity), @"environment_strength": @(renderer.params().lighting.y),
                    @"sphere_intensity": @(config.sphere_intensity), @"exposure": @(config.exposure), @"gpu": renderer.device.name,
                    @"width": @(config.width), @"height": @(config.height),
                    @"samples_per_pixel": @(renderer.accumulated*config.samples),
                    @"render_seconds": @(render_seconds), @"maximum_depth": @(config.depth),
                    @"mmd_materials": [NSString stringWithUTF8String:config.mmd_materials.c_str()],
                    @"toon_enabled": @(!config.mmd_materials.empty() && !config.no_toon && !config.physical_character),
                    @"hair_highlight_enabled": @(!config.mmd_materials.empty() && !config.no_hair_highlight && !config.physical_character && (legacy || toon_path)),
                    @"primitives": @(renderer.primitive_buffer.length/sizeof(GPUPrimitive)),
                    @"base_uv_only": @(!renderer.face_sdf_scene),
                    @"outlines_enabled": @(!config.no_outline && (legacy || config.asset_toon)),
                    @"path_outline_geometry":@(config.asset_toon && !config.no_outline),
                    @"precomputed_outline_geometry":@(config.asset_toon && !config.no_outline && config.headless && !config.dynamic_hulls),
                    @"outline_method": renderer.zzz_scene ? @"HoyoToon view-space hull; FBX vertex width; Unity material tints" : @"HoyoToon view-space backface hull; generated smooth directions; PMX width/tint; alpha cutout",
                    @"normal_maps_enabled": @(!config.no_normal),
                    @"metallic_roughness_enabled": @(!config.no_pbr),
                    @"character_self_shadows": @(has_character && !legacy),
                    @"face_sdf_enabled": @(renderer.face_sdf_scene && !(config.zzz_comparison&16) && !config.no_toon && !config.physical_character),
                    @"face_hair_shadow_enabled": @(renderer.face_sdf_scene && !(config.zzz_comparison&32) && !config.no_toon && legacy),
                    @"face_shadow_method": legacy ? @"hair-only BVH soft cone; HoyoToon shadow tint" : @"SDF directional diffuse response plus BVH light visibility",
                    @"toon_directional_light": @(config.toon_light_set),
                    @"toon_light_direction": @[@(config.toon_light.x),@(config.toon_light.y),@(config.toon_light.z)],
                    @"reflection_model": config.asset_toon ? @"Source shadow_body/specular angular response; uniform-hemisphere path integration; prepared controls enabled, inferred bindings recorded" : !has_character ? @"Standard material path tracing" : config.physical_character || (!legacy && config.no_toon) ? @"Lambert/GGX mixed BSDF with traced incident radiance" : !legacy ? @"Normalized Toon diffuse bands + compact elliptical NDF; matching mixture sampling and MIS" : renderer.zzz_scene ? @"HoyoToon ZZZ shadow_body/specular; source packed N/M/A controls" : @"Toon + GGX direct highlight + approximate sky reflection",
                    @"extended_zzz_enabled": @(renderer.extended_scene),
                    @"face_highlight_enabled": @(renderer.extended_scene && (legacy || (config.asset_toon && renderer.face_sdf_scene)) && !config.no_toon && !(config.zzz_comparison&256)),
                    @"nose_line_enabled": @(renderer.extended_scene && (legacy || config.asset_toon) && !config.no_toon && !(config.zzz_comparison&512)),
                    @"eye_layers_enabled": @(renderer.extended_scene && legacy && !config.no_toon && !(config.zzz_comparison&1024)),
                    @"layered_transparency_enabled": @(renderer.extended_scene && legacy && !(config.zzz_comparison&2048)),
                    @"stochastic_path_opacity": @(!legacy && !config.mmd_materials.empty()),
                    @"matcap_bound": @(renderer.matcap_scene),
                    @"matcap_enabled": @(renderer.matcap_scene && (legacy || config.asset_toon) && !config.no_toon && !(config.zzz_comparison&4096)),
                    @"rim_enabled": @(renderer.extended_scene && (legacy || config.asset_toon) && !config.no_toon && !(config.zzz_comparison&8192)),
                    @"source_emission_enabled":@(config.asset_toon && !(config.zzz_comparison&262144)),
                    @"rim_method": config.asset_toon ? @"HoyoToon angular rim and path-local neighbouring-ray depth mask at each hit; no screen-depth overlay" : @"HoyoToon ndotv color + screen linear depth offset",
                    @"zzz_controls_enabled": @(renderer.zzz_scene),
                    @"zzz_comparison_flags": @(config.zzz_comparison),
                    @"asset_pose": renderer.zzz_scene ? @"FBX static bind pose" : @"MMD exported pose",
                    @"camera_position": @[@(renderer.position.x),@(renderer.position.y),@(renderer.position.z)],
                    @"camera_target": @[@(renderer.target.x),@(renderer.target.y),@(renderer.target.z)]
                };
                NSError *error=nil;
                NSData *data=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:&error];
                fs::path metadata=fs::absolute(config.output);metadata.replace_extension(".json");
                if (!data || ![data writeToFile:[NSString stringWithUTF8String:metadata.string().c_str()] atomically:YES])
                    throw std::runtime_error("Cannot write render metadata");
            }
            if (view) view.renderer=nullptr;
            if (delegate) delegate.renderer=nullptr;
            if (window) {window.delegate=nil;[window close];}
            std::cout<<"Done: "<<renderer.frames<<" frames, "<<render_seconds<<" render seconds\n";
        } catch (const std::exception &error) {
            std::cerr<<"Error: "<<error.what()<<'\n';return 1;
        }
    }
    return 0;
}
