# HoyoToon attribution

The outline geometry equations, colour calculation, packed-normal decoding,
body lighting and specular equations in `pathtracer.metal`
are adapted from HoyoToon contributors' ZenlessZoneZero implementation:

- https://github.com/Hoyotoon/HoyoToon/blob/main/Shaders/ZenlessZoneZero/Include/zzz-program.hlsl (`vs_outline`, `ps_outline`)
- https://github.com/Hoyotoon/HoyoToon/blob/main/Shaders/ZenlessZoneZero/Include/zzz-common.hlsl (`outline_color`, `normal_mapping`, `shadow_body`, `specular`, `vertex_face`, `shadow_area_face`, `shadow_face`)

Upstream licence: GNU General Public License version 3 (GPL-3.0).
Licence text: https://github.com/Hoyotoon/HoyoToon/blob/main/LICENSE
These adapted portions are provided under GPL-3.0; upstream copyright remains
with HoyoToon contributors. Changes: Metal compute/BVH back-face depth pass,
world-space smooth directions authored from the MMD export, PMX material tints,
alpha clipping/open garment rejection and fixed four-sample coverage.
The ZZZ static-asset path adds FBX tangent/vertex-colour interpolation,
packed N/M/A sampling, per-region material parameters and Metal adaptations
of HLSL vector conditionals. It uses the existing scene light; it does not
implement the upstream Unity stencil or full shadow pipeline. The fifth
version adds face SDF.R/chin AO.A, original vertex face bits and UV selection,
and adapts `shadow_face`. Hair-only BVH visibility with four soft-cone samples
replaces the shifted HairShadow/stencil pass; it uses that pass's (1,.9,.9) tint
and a backend-specific 0.015-radian cone. The sixth version adds `face_high`, `nose_line`, `matcap_body`, `ndotv_rim`,
and `rim_screen_mask`, with a pinhole camera-linear depth prepass. It adds
optional vertex-red 16x16 eye-LUT decoding adapted from `seperate_eyeshadow`.
Eye/brow visibility is a ray-based, hair-only depth-limited adaptation; alpha
surfaces use deterministic front-to-back compositing, not Unity blend/stencil
passes. MatCap filenames require explicit bindings and their color space is
sRGB in this implementation. The supplied validation preset is authored and
is not a verified original game texture-ID mapping. Static head axes are explicit package settings, not animated
bone transforms.

HoyoToon is a community shader; this does not claim official game source or
original encoded outline normals. The new static package preserves its FBX
vertex colours and uses its packed material-ID textures; companion texture
bindings inferred by filename family are recorded explicitly in its JSON.

The `--asset-toon` path additionally adapts the ordering of `ps_model`, body
shadow/specular, face shadow/highlight and nose-line equations to a directional
scattering response. At every path hit it uses `f = R / (2*pi*cos(theta_i))`
and uniform-hemisphere sampling with PDF `1/(2*pi)`. This mathematical adapter
is new here; it is not a recovered game BRDF or an upstream feature. Real ray
visibility replaces raster shadow visibility. Prepared texture bindings are loaded, with their inferred/candidate evidence
retained. MatCap is adapted to the outgoing path frame and integrated as
reflectance at every hit. Camera overlays are omitted. Source outline geometry
is intersected by paths; the offset-depth rim mask is adapted using neighbouring
rays from each outgoing path origin. Original emission masks and colours are
carried as traced emitted radiance. Source JSON properties take precedence;
missing fields and textures use explicitly documented community-shader
defaults. This mode ignores authored Toon profile presets. It does not recover
the official scene lighting, pose, original engine pipeline or full material
texture bindings.
