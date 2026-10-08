#!/usr/bin/env python3
"""Generate an OptiX query adapter without editing the pinned integrator."""
from pathlib import Path
import hashlib, json
root = Path(__file__).resolve().parents[1]
source = root / 'include/pathtracer.h'
text = source.read_text()
needle = 'PT_NOINLINE bool world_hit('
assert text.count(needle) == 1
start = text.index(needle)
body = text.index('{', start)
depth = 1
end = body + 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
signature = text[start:body]
software = text[start:end].replace('bool world_hit(', 'bool software_world_hit(', 1).replace('PT_NOINLINE', 'RT_QUERY_FN', 1)
declaration = signature.replace('PT_NOINLINE', 'RT_QUERY_FN').replace(' = 0', '') + ';\n'
wrapper = signature + '''{
#if defined(__CUDA_ARCH__)
    return optix_world_hit(s, ray, t_min, t_max, rec, layer);
#else
    return software_world_hit(s, ray, t_min, t_max, rec, layer);
#endif
}
'''
declaration = declaration.replace('bool world_hit(', 'bool optix_world_hit(')
text = text[:start] + software + '\n' + declaration + wrapper + text[end:]
text = text.replace('#include "types.h"', '#include "../../include/types.h"')
text = text.replace('namespace pt {', '#ifndef RT_QUERY_FN\n#define RT_QUERY_FN PT_NOINLINE\n#endif\nnamespace pt {', 1)
# load_scene rejects these legacy configurations; omit their unreachable shaders.
legacy='color += (p.settings.z && !p.extensions.z) ? trace_character(scene,ray,p.image.w,rng,p,primary_distance) : trace_path(scene, ray, p.image.w, rng,p,primary_distance);'
overlay='if (p.settings.z && !p.settings.w) color+=hair_overlay(scene,ray,p,primary_distance);'
assert text.count(legacy)==1 and text.count(overlay)==1
text=text.replace(legacy,'color += trace_path(scene, ray, p.image.w, rng,p,primary_distance);')
text=text.replace(overlay,'// Pure packages forbid the legacy hair overlay.')
out = root / 'build/optix'
out.mkdir(parents=True, exist_ok=True)
(out / 'pathtracer.h').write_text(text)
(out / 'kernel-source.json').write_text(json.dumps({
    'source': 'include/pathtracer.h',
    'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'adaptation': 'world_hit dispatch; render entry specialized to already-enforced pure mode; path integrator, BSDF, opacity, RNG and display unchanged'
}, indent=2) + '\n')
