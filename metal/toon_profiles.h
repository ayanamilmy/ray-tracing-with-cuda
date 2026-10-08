#pragma once
// Backend-local artistic presets. These are not recovered game shader parameters.
struct ToonProfileLibrary {
    std::map<std::string,GPUToonProfile> profiles;
    NSDictionary *bindings=nil;
    NSMutableDictionary *report=[NSMutableDictionary dictionary];
    explicit ToonProfileLibrary(const std::string &path) {
        profiles["skin"]={{.22f,.58f,.48f,.83f},{1,1,.12f,.55f},{1,.95f,.92f,.35f}};
        profiles["hair"]={{.22f,.62f,.30f,.64f},{1.65f,.35f,.60f,.45f},{.96f,.98f,1,.8f}};
        profiles["fabric"]={{.28f,.65f,.24f,.64f},{1,1,.22f,.65f},{.97f,.98f,1,.85f}};
        profiles["metal"]={{.30f,.65f,.12f,.55f},{.55f,.55f,1,.15f},{1,1,1,1}};
        profiles["eye"]={{.25f,.60f,.30f,.70f},{.4f,.4f,1,.2f},{1,1,1,.5f}};
        if (path.empty()) return;
        NSData *data=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:fs::absolute(path).c_str()]];
        id doc=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nullptr] : nil;
        if (![doc isKindOfClass:[NSDictionary class]]) throw std::runtime_error("Cannot read Toon profiles: "+path);
        id definitions=doc[@"profiles"];
        if (definitions && ![definitions isKindOfClass:[NSDictionary class]]) throw std::runtime_error("Toon profiles must be an object");
        for (NSString *name in definitions) {
            id entry=definitions[name];if (![entry isKindOfClass:[NSDictionary class]]) throw std::runtime_error("Invalid Toon profile");
            const std::string key=name.UTF8String;
            if (!profiles.count(key)) throw std::runtime_error("Unknown Toon profile: "+key);
            auto &p=profiles.at(key);
            const auto vector=[&](NSString *field,F4 fallback) {
                id a=entry[field];if (!a) return fallback;
                if (![a isKindOfClass:[NSArray class]] || [a count]!=4) throw std::runtime_error("Toon vectors require four values");
                for (unsigned i=0;i<4;++i) {
                    if (![a[i] isKindOfClass:[NSNumber class]] || !std::isfinite([a[i] floatValue])) throw std::runtime_error("Invalid Toon vector");
                    fallback[i]=[a[i] floatValue];
                }
                return fallback;
            };
            p.bands=vector(@"bands",p.bands);p.lobe=vector(@"lobe",p.lobe);p.tint=vector(@"tint",p.tint);
            if (p.bands.x<=0 || p.bands.y>=1 || p.bands.y<=p.bands.x || p.bands.z<0 || p.bands.w<p.bands.z || p.bands.w>1 ||
                p.lobe.x<.05f || p.lobe.y<.05f || p.lobe.x>10 || p.lobe.y>10 || p.lobe.z<0 || p.lobe.z>1 || p.lobe.w<.12f || p.lobe.w>1)
                throw std::runtime_error("Toon bands/lobes outside allowed range");
            for (unsigned i=0;i<4;++i) if (p.tint[i]<0 || p.tint[i]>1) throw std::runtime_error("Toon tint must be 0 to 1");
        }
        bindings=doc[@"materials"];
        if (bindings && ![bindings isKindOfClass:[NSDictionary class]]) throw std::runtime_error("Toon material bindings must be an object");
    }
    void apply(GPUMaterial &m,const std::string &name) {
        if (m.flags.x!=4) return;
        const unsigned type=unsigned(m.zzz_misc.y);
        std::string fallback=type==4 ? "hair" : type==1 || type==3 ? "skin" : type==2 ? "eye" : "fabric";
        NSString *key=[NSString stringWithUTF8String:name.c_str()];id entry=bindings[key];
        if (entry && (![entry isKindOfClass:[NSArray class]] || [entry count]!=5)) throw std::runtime_error("Toon binding needs five region names: "+name);
        NSMutableArray *names=[NSMutableArray array];
        for (unsigned i=0;i<5;++i) {
            std::string profile=fallback;
            if (entry) {
                if (![entry[i] isKindOfClass:[NSString class]]) throw std::runtime_error("Invalid Toon profile name");
                profile=[entry[i] UTF8String];
            }
            if (!profiles.count(profile)) throw std::runtime_error("Unknown Toon profile: "+profile);
            m.toon_profiles[i]=profiles.at(profile);
            [names addObject:[NSString stringWithUTF8String:profile.c_str()]];
        }
        m.toon_metal=profiles.at("metal");report[key]=names;
    }
};
