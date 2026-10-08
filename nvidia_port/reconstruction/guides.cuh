// Auxiliary first-hit data. Lighting remains in the original path integrator.
namespace reconstruction_guides {
PT_FN pt::float2 project(const pt::GPUParams &p,pt::float3 position) {
    using namespace pt;
    auto rel=position-p.origin.xyz;
    auto back=normalize(cross(p.right.xyz,p.up.xyz));
    float z=-dot(rel,back);
    float focus=-dot(p.lower_left.xyz+.5f*p.horizontal.xyz+.5f*p.vertical.xyz-p.origin.xyz,back);
    if(z<=1e-6f)return f2(0);
    return f2(.5f+dot(rel,p.right.xyz)*focus/(z*length(p.horizontal.xyz)),
              .5f+dot(rel,p.up.xyz)*focus/(z*length(p.vertical.xyz)))*f2(p.image.xy);
}
}
extern "C" __global__ void __raygen__guides() {
    using namespace pt;
    unsigned i=optixGetLaunchIndex().x;
    auto p=launch.params;if(i>=p.image.x*p.image.y)return;
    float2 uv=(f2(i%p.image.x,i/p.image.x)+.5f)/f2(p.image.xy);
    Ray ray{p.origin.xyz,normalize(p.lower_left.xyz+uv.x*p.horizontal.xyz+uv.y*p.vertical.xyz-p.origin.xyz)};
    Scene scene{launch.primitives,launch.nodes,launch.materials,launch.pixels,&p,nullptr};
    Hit h{};bool found=false;float minimum=.00001f;
    for(unsigned j=0;j<64;++j) {
        if(!world_hit(scene,ray,minimum,10000,h))break;
        if(launch.materials[h.material].flags.x!=4||mmd_base(scene,h).a>=.5f){found=true;break;}
        minimum=h.t+.00001f;
    }
    launch.guide_albedo[i]=launch.guide_specular[i]=launch.guide_normal[i]=launch.guide_motion[i]=f4(0);
    launch.guide_depth[i]=10000;launch.guide_roughness[i]=1;
    if(!found)return;
    auto back=normalize(cross(p.right.xyz,p.up.xyz));
    launch.guide_depth[i]=max(.00001f,-dot(h.p-p.origin.xyz,back));
    // Same point on the previous frame's triangle, rather than camera-only flow.
    float3 previous=h.p;
    if(!launch.guide_reset) {
        const auto &g=launch.previous_primitives[h.primitive];
        previous=g.meta.x?h.bary.x*g.a.xyz+h.bary.y*g.b.xyz+h.bary.z*g.c.xyz:
            g.a.xyz+h.geometric*g.a.w;
        auto now=reconstruction_guides::project(launch.nominal_camera,h.p);
        auto old=reconstruction_guides::project(launch.previous_camera,previous);
        launch.guide_motion[i]=f4(old-now,0,0); // current -> previous, input pixels, bottom-up
    }
    if(h.outline){launch.guide_albedo[i]=f4(launch.materials[h.material].edge_color.xyz,1);return;}
    apply_normal_map(scene,h,p);
    if(dot(h.normal,ray.direction)>0)h.normal=-h.normal;
    launch.guide_normal[i]=f4(normalize(h.normal),0);
    if(launch.materials[h.material].flags.x==4) {
        auto b=character_bsdf(scene,h,p);
        launch.guide_albedo[i]=f4(b.base,1);
        // Source Toon transport has no measured PBR roughness. Its diffuse
        // guide uses the existing base color; do not fabricate official values.
        launch.guide_roughness[i]=b.source?1:clamp(b.roughness,.001f,1.f);
        if(!b.source)launch.guide_specular[i]=f4(mix(f3(.04f),b.base,b.metallic),1);
    } else {
        const auto &m=launch.materials[h.material];
        launch.guide_albedo[i]=f4(m.color.xyz,1);
        if(m.flags.x==1){launch.guide_specular[i]=f4(m.color.xyz,1);launch.guide_albedo[i]=f4(0);launch.guide_roughness[i]=max(.001f,m.color.w);}
    }
}
