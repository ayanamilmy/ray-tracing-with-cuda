# 独立 Metal 版

这是 `main_accum.cu` 的 Metal 实现，渲染在 Mac GPU 上运行。CPU 负责读取 OBJ/图片、建立 BVH、处理输入和显示窗口；求交、路径追踪、贴图采样、累计采样、双边滤波和显示颜色转换由 Metal 计算着色器执行。

## 文件

| 文件 | 用途 |
| --- | --- |
| `main.mm` | Objective-C++ 主程序：资产上传、CPU 建树、相机、窗口、Metal 调度、PNG 保存 |
| `pathtracer.metal` | GPU 求交、材质与光照、MIS、累计采样、双边滤波、显示转换 |
| `shared_types.h` | CPU/GPU 共用的数据布局，主程序用静态断言检查大小和偏移 |
| `build.sh` | 编译脚本，生成 `build/metal_accum` |
| `../shared/asset_io.h` | 与 CUDA 共用的 OBJ / MTL / 图片读取器 |

## 构建

需要 macOS 13 或更新版本、支持 Metal 的 GPU、以及 Xcode 的 C++ 编译工具。当前机器已用 M1 Pro 构建成功。

在仓库根目录执行：

```sh
./metal/build.sh
```

如果有离线 Metal 编译组件，脚本会生成 `pathtracer.metallib`；没有这个组件时，程序启动后通过 Metal 框架编译放在可执行文件旁边的着色器。当前机器使用后一种方式，不需要为此下载额外组件。

`build/` 包含生成文件，已加入 Git 忽略规则。移动可执行文件时，也要带上相邻的 `.metallib`，或者 `pathtracer.metal` 和 `shared_types.h`。

只检查着色器和 GPU 管线，不创建场景、窗口或执行渲染：

```sh
./metal/build/metal_accum --check
```

## 交互运行

位置参数与 CUDA 版相同：

```sh
./metal/build/metal_accum
./metal/build/metal_accum 4 0 0.15 720 360 "assets/character.obj"
```

依次为每帧每像素样本数、运行秒数、双边滤波颜色参数、宽度、高度、可选 OBJ 路径。样本数必须是完全平方数；运行秒数为 `0` 表示不限，滤波参数为 `0` 表示关闭滤波。

右键拖动旋转视角，中键平移，滚轮前后移动；按住右键配合 `W/A/S/D/Q/E` 飞行；`F` 注视原点；空格切换静止累计采样；`Esc` 退出。相机变化会清空此前累计的样本。窗口保持画面比例。

`P` 保存当前画面为 `metal-render.png`。可用 `--output 路径.png` 指定保存位置；指定输出时退出前也会保存。

## 离线静态输出

以下是使用方法，本次编写过程中没有运行这条渲染命令：

```sh
./metal/build/metal_accum 4 0 0.15 1280 720 "assets/character.obj" \
  --headless --frames 16 --output "renders/character.png"
```

`--headless` 关闭交互窗口，保持同一相机累计采样。上例是 16 帧乘以每帧 4 个样本，合计每像素 64 个样本。`--frames N` 限制帧数，运行秒数可限制时间，先达到的条件结束；二者都没指定时渲染一帧。

额外可选参数为 `--depth N`、`--aperture F`、`--focus F`、`--fov F`。默认仍是 CUDA 版的最大深度 50、光圈 0.1、焦距 10、垂直视角 20 度。`--aperture 0` 可以关闭景深。文件路径按启动程序时的工作目录解释。

## 与 CUDA 对齐的内容

两版使用相同的三角形求交与属性插值、最近邻底色图片采样、线性颜色计算、漫反射/金属/玻璃模型、梯度天空、位于 `(0,5,0)` 且半径为 1 的发光球、直接光源采样和幂启发式 MIS。每帧采用分层采样，静止时累计均值，再按相同参数做双边滤波和 sRGB 显示转换。相机初始位置、景深和默认渲染参数保持对应。

两版不是逐像素相同的输出：Metal 使用独立的随机数初始化，随机小球的具体位置和材质颜色也会不同。Metal 在 CPU 上按空间中位数建树，CUDA 保留原来的建树实现；浮点运算和随机路径也可能不同。这些后端差别不改变 OBJ 的数据读取规则及所使用的光照公式。当前 CUDA 金属材质使用未归一化的入射方向，Metal 也保留了这一现有行为。

OBJ 的支持范围、导出要求和材质限制见 [资产说明](../ASSET_SUPPORT.md)。普通 OBJ 默认继续使用原路径追踪。下面的 MMD 材质分支只在 Metal 启用；未新增骨骼动画、俄罗斯轮盘赌或路径引导。

## MMD 第一版：toon 与独立头发高光层

`--mmd-materials 文件.json` 按 OBJ 材质名绑定原模型的 toon 配色贴图、sphere-map、透明度和投影开关。基础颜色与高光层颜色仍使用基础 UV；toon 按表面与主光的夹角查表，sphere-map 按相机坐标中的法线查表。两者均不会使用额外 UV。第一版不把角色自阴影混入 toon 配色，避免重叠的身体/衣服表面造成三角形斑块；普通地面仍接收透明度感知的角色投影。JSON 的 `self_shadow` 暂仅保留原数据。

MMD 表面的颜色由 toon 分支直接计算；地面和其他普通材质仍使用原路径追踪。颜色贴图和 toon 按 sRGB 解码，透明度保留线性数值；加法 sphere-map 按 MMD Tools 的约定读取线性值。MMD 贴图使用双线性过滤，透明表面使用随机透过并累计样本。独立头发高光层读取原 `sp.png` 和 `hair_s.bmp`，按透明度加法叠色；它不遮挡普通表面，也不作为不透明阴影遮挡物。

这是一套基础 MMD 材质支持，未实现 HoyoToon 的脸部 SDF、描边、特殊眼睛遮挡等功能。高光层的透明度和加法叠色属于独立处理，不是实际发光光源。

准备资产时，使用 `prepare_mmd_asset.py` 从现有 Blender 第一帧恢复 PMX 的头发高光层，并保留原 toon/sphere 绑定。该脚本只读取原 blend/PMX，生成新的 OBJ、MTL、材质 JSON 和贴图副本，**不会保存覆盖原 blend**。需要提供现有的 MMD Tools PMX 解析器路径。

```sh
"/path/to/Blender" --background --factory-startup --python metal/prepare_mmd_asset.py -- \
  --blend source.blend --pmx model.pmx --parser pmx_reference.py --output mmd-package

./metal/build.sh
./metal/build/metal_accum 16 0 0 2560 1440 mmd-package/character.obj \
  --mmd-materials mmd-package/character.mmd.json --character-scene \
  --headless --frames 16 --aperture 0 --fov 28 --output character-toon.png
```

上例每轮 16 样本、累计 16 轮，合计 256 样本/像素。`--character-scene` 使用角色、原地面和球形灯，并设置为本次角色静态图的相机。`--no-toon`、`--no-hair-highlight` 可单独关闭功能作对照检查；默认都启用。

GPU 材质结构和新增参数仅位于 `metal/`，CUDA 和共享 OBJ 加载器不需要改变。完整额外 UV、描边、法线/金属/粗糙度贴图不属于本次改动。

## 已做的检查

- Objective-C++ 主程序构建成功。
- 在这台 M1 Pro 上成功编译三个 Metal 着色器并建立计算管线；检查模式没有执行渲染。
- 资产读取检查通过：独立与负数索引、UV 接缝、顶点法线、材质分组、嵌套 MTL 路径、图片行序和非法输入处理。
- CUDA 属性插值及贴图数学代码通过了 CPU 替身检查；这不是实际 CUDA 编译。Mac 没有 NVCC，CUDA 版需要在 Windows 上编译验证。

本次未启动交互渲染或离线出图，因此未检查实际画面与帧率。

## 描边、法线贴图与金属/粗糙度（第二版）

仅更改 `metal/`。原 OBJ/MTL、CUDA、共享资产读取器和第一版图片保留。

- 描边已替换为 HoyoToon 的视空间背面外扩算法（第三版）：外扩 XY 方向归一化，Z 为 0.0001，宽度 `edge_size × outline_scale × 0.0015` 米；FOV/深度参与微小的视空间位置偏移。Cull Front 与可见表面的深度测试在计算着色器中用 BVH 求交实现，每像素四个固定子样本抗锯齿。原网格和路径追踪 BVH 不增加膨胀三角形，描边遍历对 BVH 包围盒加最大材质线宽 + 0.002 米余量。
- 这份 PMX 没有已确认符合 HoyoToon 编码的 UV3/顶点色数据，因此在 Metal 主程序内按位置与材质焊接、按角度加权生成专用平滑外扩方向，局部宽度设为 1。保留基础 UV 和原着色法线。原 PMX 开关/宽度/颜色分别替代 Outline 开关/宽度/材质 ID 色表；描边色使用 HoyoToon `pow(tint*0.5,1.5)`、基础贴图和光向混合。没有使用未经确认的额外 UV。
- 针对 MMD 开放透明衣料额外使用 alpha≥0.5 裁切，并拒绝射线同时命中未外扩同一三角形的内部像素，避免显露背面形成黑色三角块。这是本资产适配，Unity 原 `ps_outline` 没有这项裁切。描边使用针孔相机，景深为零时与渲染匹配。
- 参考：[HoyoToon vs_outline/ps_outline](https://github.com/Hoyotoon/HoyoToon/blob/main/Shaders/ZenlessZoneZero/Include/zzz-program.hlsl#L365)、[outline_color](https://github.com/Hoyotoon/HoyoToon/blob/main/Shaders/ZenlessZoneZero/Include/zzz-common.hlsl#L1093)。这是社区 HoyoToon 算法的 Metal 适配，不是原游戏着色器或完整 HoyoToon 移植。HoyoToon 代码采用 GPL-3.0，参见同目录 `HOYOTOON_NOTICE.md`。
- 法线贴图使用基础 UV、线性读取；由三角形位置与 UV 推导切线坐标系，保留镜像 UV 的方向。UV 退化时跳过。只改变 Toon 与反射使用的着色法线，不改变几何或射线偏移。默认 OpenGL 正 Y，DirectX 可用 `normal_flip_y`。
- 金属度/粗糙度贴图线性读取，可以选择 RGBA 通道。使用 GGX 高光与廉价的天空环境反射近似，并和 Toon 颜色混合。**角色分支不追踪其他物体的反射**；自阴影仍未混入 Toon。粗糙度最小为 0.08，防止点光近似产生极端高光。
- 算法参考：[Filament 材质模型说明](https://google.github.io/filament/main/filament.html)。这不是完整的物理路径追踪材质，更不是原游戏着色器。

JSON 每材质可添加：

```json
{
  "normal_texture": "maps/normal.png",
  "normal_strength": 0.5,
  "normal_flip_y": false,
  "metallic_texture": "maps/metallic.png",
  "metallic": 1.0,
  "metallic_channel": 0,
  "roughness_texture": "maps/roughness.png",
  "roughness": 1.0,
  "roughness_channel": 0,
  "reflection_mix": 0.65,
  "edge_enabled": true,
  "edge_size": 0.5,
  "edge_color": [0.0, 0.0, 0.0, 1.0]
}
```

`metallic`/`roughness` 是贴图的乘数；没有贴图时直接使用常量。RGB 法线颜色为切线空间方向，不是明暗图。`reflection_mix=0` 保持第一版 Toon 输出。老 JSON 没有这些字段时保持第一版样式。

本资产没有随附独立法线、金属度、粗糙度贴图。`prepare_surface_preset.py` 为演示生成轻微布料法线、按基础颜色选取的金色区域遮罩和粗糙度图，JSON 写明 `surface_map_provenance`。**这些是本次自制的预设，不是解包得到的游戏控制图**；遮罩可自行编辑，金色不必然等于金属。脸和皮肤不加凹凸，眼睛与原头发高光层不加这套反射。

```sh
python3 metal/prepare_surface_preset.py \
  --asset path/to/mmd-package --pmx path/to/model.pmx \
  --parser path/to/pmx_reference.py --output path/to/surface-package
```

脚本需要 NumPy/Pillow，只读取原数据并写入新目录。渲染仍使用第一版 OBJ，改用新的 `character.surface.json`：

```sh
./metal/build.sh
./metal/build/metal_accum 16 0 0 2560 1440 path/to/character.obj \
  --mmd-materials path/to/character.surface.json --character-scene \
  --headless --frames 16 --aperture 0 --fov 28 \
  --camera 0.9 1.05 3.75 --target 0.03 0.87 -0.1 --output front.png
```

可用 `--camera X Y Z` / `--target X Y Z` 指定视角，`--outline-scale` 调整线宽，`--no-outline` / `--no-normal` / `--no-pbr` 分别作功能对照。PNG 旁的 JSON 记录尺寸、实际累计采样、视角、功能开关和耗时。

## ZZZ 原始控制贴图支持（第四版）

独立的 `prepare_zzz_asset.py` 将下载包的二进制 FBX 导出为静态 OBJ，保留基础 UV、顶点法线、原始切线及顶点色。`attributes.bin` 保存 OBJ 无法表达的逐三角形切线/顶点色；加载时校验数量、长度和有限数值。需要 Python/NumPy，不改动原始资产。

```sh
python3 metal/prepare_zzz_asset.py --source path/to/original --output path/to/package
./metal/build/metal_accum 16 0 0.15 3840 2160 path/to/package/character.obj \
  --mmd-materials path/to/package/character.zzz.json --character-scene \
  --headless --frames 16 --depth 50 --aperture 0 --fov 28 \
  --camera 0.16 1.50 1.38 --target 0.02 1.365 -0.1 --output bust.png
```

- D：基础色；N.RG：切线空间法线，重建 Z，N.B：身体光照控制。
- M.R：五区材质 ID，M.G：金属度，M.B：高光遮罩；A.G：光滑度。
- 接入原材质 JSON 中相应的颜色、阴影、高光、金属度、光滑度、法线强度与描边参数。缺失的字段使用 HoyoToon shader 默认值，未使用此前自制的法线/金属/粗糙度图。
- 身体和头发采用 HoyoToon `shadow_body`/`specular` 的 Metal 适配；新分支不再用旧 GGX/天空反射预设。描边保留背面外扩，加入源顶点色 R 的宽度和 M.R 分区描边色。
- D 的连接由 FBX 确认；JSON 的贴图引用只有文件 ID、没有名字，因此 N/M/A 按相同文件名前缀配对，配对依据写入 `binding_evidence`，不能称为已核实的原始贴图 ID 映射。
- 当前是 FBX 静态绑定姿态，不含运行时骨骼动画。脸/眼使用基础色；脸 SDF、MatCap、专用头发阴影、Stencil、角色自阴影和完整游戏光照尚未接入。此次没有启用额外 UV；UV3 与 UV0 重复，描边方向仍采用生成的平滑方向。

对照开关：`--no-normal`、`--no-light-control`、`--no-material-id`、`--no-specular-mask`、`--no-metallic`。所有修改限制在 `metal/`；CUDA 和共享加载器保留。新版本的实际检查及渲染信息记录于 PNG 旁 JSON，前文早期版本的未出图说明不适用于此版本。

## 脸部 SDF 与头发投脸阴影（第五版）

仅修改 Metal。`prepare_zzz_asset.py` 新增 `--face-lightmap Female_Face_Lightmap_02.png`（默认）或 `Female_Face_Lightmap.png`；新输出目录不会覆盖此前包或原始资产。**两者都没有已核实的原 Unity 文件 ID 对应关系，当前选择是明确记录的候选绑定。**

- 脸部 Lightmap 线性读取 R（SDF）和 A（下巴 AO）。按 HoyoToon `vertex_face` 解码顶点色 B 的 bit4 为脸部标记、低两位为材质区；逐顶点选择 UV0/UV1，再按头部右向量与光向量的点积镜像 U。
- 按 `shadow_area_face` 计算 `smoothstep(threshold-.5, threshold+.5, R*.9+.1)*AO`，再调用移植的 `shadow_face` 分段颜色。眼睛/牙齿等无脸部标记区域不套下巴 AO。使用原材质 JSON 阴影色和已有 HoyoToon 默认 post tint；不人为加深阴影。
- `attributes.bin` 升级为 `ZZZATTR2`：每角点 12 个 float（切线4、顶点色4、UV1.xy、脸部标记、材质ID），保留 `ZZZATTR1` 读取兼容性。GPU 结构只位于 Metal。未指定脸部贴图的旧包保留原来的脸部样式。
- `head_forward`/`head_right` 是材质 JSON 的三分量单位轴。此包为静态绑定姿态，设为世界 +Z/+X；已通过左右侧光灰度图检查方向。以后旋转/动画角色时必须同步更新头部轴，当前没有自动骨骼驱动。
- 头发阴影仅允许 ZZZ Hair 材质投射到有脸部标记的区域，排除皮肤三角形自遮挡。使用原始头发几何、透明度、4 个固定小锥角方向，阴影色为 HoyoToon HairShadow pass 的 `(1,.9,.9)`。这是 BVH 遮挡适配，**不是原 shifted mesh/Unity Stencil 的完整移植**；锥角 0.015 弧度是 Metal 实现参数。
- `--light-direction X Y Z` 控制 ZZZ Toon 的光源方向（朝向光源），供侧光对照；不会移动物理路径追踪发光球。不设置时仍使用原发光球中心方向。
- `--no-face-sdf` / `--no-hair-shadow` 分别关闭；`--debug-face-sdf` / `--debug-hair-shadow` 在脸部显示灰度控制值。脸部 G 高光、鼻线、眼部 Stencil、MatCap、全身自阴影仍不在此轮范围。

此轮已在 M1 Pro 实际编译、渲染左/正/右光与独立开关对照，并检查 SDF 灰度亮区随左右光换边、刘海阴影遮罩；核对 CUDA/共享文件及原始资产哈希不变。


## 扩展材质与显示规则（第六版）

本轮只改 `metal/`。旧包没有 `zzz_extended: true` 时保持第五版规则；原始资产不改写，新包放在独立目录。

```sh
python3 metal/prepare_zzz_asset.py --source ../outputs/ye-shunguang-hoyotoon-assets/original \
  --output ../outputs/ye-shunguang-metal-zzz-v6 --extended \
  --matcap-bindings metal/presets/ye_shunguang_matcap_candidate.json
./metal/build.sh
./metal/build/metal_accum 16 0 0.15 3840 2160 ../outputs/ye-shunguang-metal-zzz-v6/character.obj \
  --mmd-materials ../outputs/ye-shunguang-metal-zzz-v6/character.zzz.json --character-scene \
  --headless --frames 16 --aperture 0 --fov 28 \
  --camera 0.16 1.5 1.38 --target 0.02 1.365 -0.1 --light-direction 0 0.4 1 \
  --output ../outputs/ye-shunguang-render/ye_shunguang_zzz_v6_bust_4k_256spp.png
```

新增支持：

- **脸部细节**：Face Lightmap.G 配合脸部顶点标记、UV 鼻区和半程向量产生高光。原始 D.a 独立参与鼻线，不把整张脸变透明。读取源 `_NoseSpecularScale`、`_NoseSmoothX/Y`，缺失采用默认值；没有游戏脚本随视角动态调整阈值。
- **透明/裁切/剔除**：逐层前后合成最多 64 个表面。显式 `opacity_mode` 为 0 不透明、1 裁切、2 混合；`alpha_source` 为 0 D.a 或 1 A.r。`_Cutoff` 控制裁切，`_Cull` 为 0 双面、1 剔前、2 剔后。源 `_DoubleSided` 启用时背面可用保留的 UV1。导出策略：`_AlphaClip` 优先裁切，`_T_UI` 用 A.r 混合，眼眉用 D.a，脸部不透明。这是明确的 Metal 策略，未恢复完整游戏渲染队列/自定义 Blend 状态。
- **眼眉显示**：普通 Eye/Eyebrow 保留 D 颜色；眼眉在不超过 `eye_reveal_depth`（默认 0.08 米）的头发后，可用头发 A.r 和 `_MinStencilAlpha` 控制显露。任何非头发遮挡都会拒绝显露，背面也拒绝。不是原 Unity Stencil。`eye_role`：0 普通、1 眼、2 眉、3 独立眼影、4 独立眼高光；后两者可显式绑定 `eye_color_map`（必须 16×16），按顶点 R 的高低四位查色。当前这套资产没有接入独立眼影/高光网格，也没有把 Eye_E 强行绑定到普通眼睛。
- **MatCap**：按 M.r 选择五个区域，从视空间法线取 UV，A.b 控制覆盖；支持原材质的 tint、color/alpha burst、alpha/add/overlay 三种混合，以及静态时间的 UV 滚动/折射 UV。`matcap_textures` 必须显式给出五个路径或 null，读取为 sRGB。`effect_time` 手动控制时间，当前不会自动播放这些材质动画。原 JSON 的匿名 ID 无法证明文件名对应关系；随附 preset 是**人工候选验证绑定**，证据保存在每个材质的 `binding_evidence`。不传 `--matcap-bindings` 时保留未绑定状态。
- **轮廓光**：移植 HoyoToon `ndotv_rim`，读取五区 `_RimGlowLightColor` / `_UISunColor` 与 `_RimWidth`。先渲染屏幕线性深度，再沿视空间法线偏移采样，只有深度发生变化的边缘得到 rim。深度按中心针孔射线和有效不透明度 ≥0.5 记录，与 Unity 深度写入策略存在差别。

独立对照：`--no-face-highlight`、`--no-nose-line`、`--no-eye-layers`、`--no-transparency`、`--no-matcap`、`--no-rim`。调试：`--debug-alpha`、`--debug-matcap-mask`、`--debug-rim`、`--debug-eye-layers`。前上方灯为 `(0,0.4,1)`；这是本轮新图明确采用的灯位，区别于之前顶部灯。

实际 GPU 图像检查：

```sh
python3 metal/validate_extended.py --asset ../outputs/ye-shunguang-metal-zzz-v6 \
  --output ../work/toon-v6-backup
```

需要 numpy/Pillow；测试输出包含独立开关、正侧背视角、旧包图像对照和重叠平面的透明/裁切/剔除/眼眉遮挡、五个 MatCap 槽位独立采样测试。测试对照旧包需要工作区上一轮 `work/toon-v5-backup/old-package.png`；它是本次开发的图像回归检查，不是通用资产导入器测试。

## 卡通角色的多跳光照（2026-10-05）

角色的间接路径现在使用 Lambert/GGX 混合 BSDF、球灯直接采样、MIS 和俄罗斯轮盘赌。后续命中角色会继续反弹，不把 Toon/MatCap 成品色当发光。相机可见的 SDF、MatCap、高光和描边保留。当前使用相关天空基线的光照修正：`Toon + strength × [直接光估计 + BSDF权重 × (实际入射光 − 相同方向的天空)]`，避免在已有天空填充上重复加环境光。默认 `--indirect-strength 0.35`，设为 0 可与旧版比较。深度 1 禁用间接路径。

这是带真实多跳光照的风格化渲染；相机着色保留艺术规则，并非整个角色的严格物理 BSDF 渲染。透明层仍按原有规则合成。间接路径支持基础色、法线和控制贴图；暂不支持将 MatCap、轮廓光或匿名自发光参数作为实际场景光源。

`--physical-character` 切换到完整材质路径追踪：主光线命中角色同样走 BSDF、灯光采样与后续反弹，不调用相机可见 Toon/SDF/MatCap/轮廓光和高光覆盖，描边关闭。该模式会改变外观；默认模式为保留卡通外观的真实路径光照修正。两种模式的名称和行为不能混淆。

## 全程卡通路径追踪（2026-10-05，替代上述默认混合模式）

加载角色材质时默认走 **Toon BSDF 全路径积分**。相机首个命中与随后每个角色命中都使用同一个材质求值函数；直接光采样、BSDF 随机反弹、球灯 MIS、俄罗斯轮盘赌共同计算光照。没有 `Toon + 间接修正`、天空差值、相机成品着色覆盖。`--legacy-character` 才选择上一节的旧混合模式；`--physical-character` 选择 Lambert/GGX，`--no-toon` 在新路径中也选择连续 Lambert/GGX 响应。

- 身体：三档漫反射，在余弦项相消后形成明确亮暗区；掠射分母设下限，白色漫反射分量的半球积分归一化。ZZZ 分区颜色、阴影色作为受光材质颜色。
- 脸：现有 Face SDF 按**每个入射方向**取左右值、比较头部朝向阈值，控制分档反射。使用保守漫反射能量上限，实际光照与遮挡依然由路径计算。头部轴仍为资产包静态坐标，未加入动画。
- 高光：椭圆投影盘构成微表面法线分布；头发在盘内从中心向边缘渐暗，其他 Toon 材质保留硬边界。资产切线决定头发高光长轴。显式采样该分布并使用匹配的混合 PDF；无效反射方向作为空样本终止，不能反复抽到成功。M.G 金属度、M.B 高光遮罩、A.G 光滑度和原材质颜色参与响应。
- 光源：原球灯、渐变天空，以及有遮挡检测的方向光。`--light-direction` 在新模式中是实际方向光的方向，默认角色场景为 `(0,.4,1)`；`--sun-intensity 2.8`、`--environment-strength .25`、`--sphere-intensity 1` 为默认强度，`--exposure 1` 只控制显示。
- 原 MatCap、屏幕轮廓光、头发覆盖高光、眼睛穿透头发显示与外扩描边不进入新模式。透明度/裁切采用每条路径随机穿透表面的估计，法线贴图和真实几何遮挡保留。这一版不是 HoyoToon 全部规则的一比一移植。

这是**风格化反射函数的蒙特卡洛路径积分**。分档漫反射/SDF 是艺术规则，不宣称为满足物理互易性的 PBR 材质；它们与 Lambert/GGX 视觉目标不同。多跳路径不会自动等于游戏原画，参数和照明仍需调节。有限深度会截断更长路径；`--depth 8` 为最多八次表面命中、七次继续反弹，俄罗斯轮盘赌可能提前结束。

```sh
./metal/build.sh
./metal/build/metal_accum 16 0 0.08 960 540 ../outputs/ye-shunguang-metal-zzz-v6/character.obj \
  --mmd-materials ../outputs/ye-shunguang-metal-zzz-v6/character.zzz.json --character-scene \
  --headless --frames 16 --depth 8 --aperture 0 --fov 24 \
  --camera .03 1.425 .55 --target 0 1.405 -.055 --light-direction 0 .4 1 \
  --output ../outputs/ye-shunguang-render/ye_shunguang_full_toon_path_face.png
```

设全部三个光源强度为零，新模式的线性输出和 PNG 都应为黑色。PNG 旁 JSON 记录实际材质模式、光源强度、深度与关闭的旧显示规则。修改范围仅 `metal/`，不改 CUDA、共享加载器与原始资产。

## 角色布光与分材质规则（2026-10-05）

仍为全路径卡通材质积分，只改 Metal；原资产、共享加载器、CUDA 不改写。新增三项：

1. **温和色调映射**：线性曝光后，以最高 RGB 通道控制指数高光肩部；默认 knee=.7，低于 knee 的颜色保持原值，高于 knee 逐渐压向 1。所有通道使用同一比例，保留 RGB 比例，之后才转 sRGB。`--no-tonemap` 对照原硬裁切，`--tone-knee .7` 可调压缩起点。它是显示变换，不给场景添光；默认旧混合模式不启用。
2. **可调矩形面光源**：重复 `--area-light CX CY CZ TX TY TZ WIDTH HEIGHT R G B INTENSITY`，最多四盏。中心/目标定义位置与朝向，尺寸为世界单位全宽高；RGB×INTENSITY 是线性发光辐射亮度。每盏灯作为两个真实发光三角形加入 BVH，单面发光，可被射线命中，也可遮挡别的光源。直接采样按亮度×面积选择灯，再均匀取矩形点；面积 PDF 转为立体角 PDF，并乘选择概率。BSDF 命中灯时使用同一 PDF 做 MIS，防止双计。改变尺寸会改变总发光功率，需相应调整亮度。灯默认不自动添加，脚本提供三灯配置。
3. **分材质 Toon 预设**：默认按角色材质类型选择 skin/hair/fabric/eye，原金属度在 .35–.8 内连续混入 metal 规则。`--toon-profiles path.json` 可覆盖四分量 bands/lobe/tint，并按 M.R 五区域绑定材质；`--uniform-toon` 对照上一版统一规则。bands 是暗部/中部阈值与两级亮度；lobe 是椭圆高光宽度比例 XY、高光强度、粗糙度下限；tint 是阴影颜色乘数 RGB 与原阴影色混合权重。归一化随分档参数解析计算；椭圆求值、采样、PDF 同步使用调整后的宽度。脸 SDF 同样作为每个入射方向的反射规则，不绕过路径光照。

`presets/ye_shunguang_toon_profiles.json` 是**人工艺术预设**，不是解包取得的游戏材质配置。Body/Leg 的 >=.8 区域按检查 D/M 图后指定为皮肤，其他区域先用布料；原 M.G 继续决定金属响应。分区绑定可编辑，不能当作已验证的官方分区语义；未修改原材质 JSON。预设和实际绑定写入渲染元数据。原 MatCap/屏幕轮廓光/描边覆盖依然关闭。

复现同视角 2560×1440、256spp 三灯图：

```sh
./metal/build.sh
./metal/render_ye_shunguang_studio.sh
# 可在输出文件名之后覆盖其他 CLI 参数；例如快速低采样检查：
./metal/render_ye_shunguang_studio.sh /tmp/studio-preview.png --frames 1
```

三灯分别是偏左上方的暖主灯、右前方较弱的冷补灯、右后方冷边缘灯；边缘亮部由真实反射产生。这里没有添加假轮廓光或图像覆盖。预设是静态绑定姿态的布光方案。

GPU/数值检查：

```sh
python3 metal/validate_studio.py --output /tmp/metal-studio-check
```

需要 numpy/Pillow。测试将矩形灯照白平面的结果与独立密集面积积分对照，检查多灯选择 PDF、直接估计与 MIS、一块真实遮挡板、HDR 彩色发光面从硬裁切白色恢复为有色亮部，以及各预设的漫反射归一化与混合高光采样积分。通过这些检查不等于卡通 BSDF 满足物理互易性；这一版依然是艺术反射模型。

## 头发柔化与 4K 胸像

仅 ZZZ 头发类型使用渐隐椭圆 NDF：投影盘内 `D=2*(1-r²)/(π*ax*ay)`，盘外为零。其径向累计概率为 `2s-s²`（`s=r²`），采样使用逆函数 `s=u/(1+sqrt(1-u))`；直接光照、反弹求值和 MIS 都使用相同密度。仍使用资产切线，不增加相机覆盖高光。

头发 lobe 改为 `[1.65,.35,.60,.45]`，暗档由 .22 提到 .30。两个明暗边界各最多 ±.045 的范围内平滑混合受光颜色，边界以外保留三档。过渡窗口保持对称、不重叠，并避开余弦分母的 .12 下限；其积分与原阶跃相同，现有解析归一化仍适用。窗口无法放置时安全回退到硬分档。

复现同三灯布光的 3840×2160、256spp、深度 8 半身胸像：

```sh
./metal/build.sh
./metal/render_ye_shunguang_bust.sh
```

这是发束表面的风格化反射材质，未加入真实发丝几何、动画或纤维内部散射。

## 原资产参数路径模式（2026-10-05）

`--asset-toon` 读取 `source_material_json` 原文件的标量和颜色，保留所有整理包中
已有的真实贴图，并记录每个绑定的来源。默认接入 D、N/M/A、Face SDF 和五区
MatCap；文件名配套推断和人工候选映射仍明确标为推断，不能叫官方绑定。
`--strict-texture-bindings` 是可选对照，用于只保留已确认的绑定。
不加载人工 Toon profiles，不把头发金属度强行改成 0，不增加粗糙度下限，
不覆盖资产中的高光形状、光滑度、明暗颜色和原效果开关。

当前静态角色的数据通道：

- D.rgb 为基础色；D.a 参与眼眉透明度及鼻线。
- N.rg 为切线空间法线；N.b 参与受光分档。
- M.r 选五个材质区；M.g 为金属度；M.b 为高光遮罩。
- A.r 为透明表面覆盖率；A.g 为光滑度；A.b 为 MatCap/发光遮罩。
- Face SDF.r 决定每个入射方向的明暗，g 控制鼻部高光，a 为下颌 AO；
  原 UV1、顶点蓝通道位标记和材质 ID 决定采样位置。
- 原切线、顶点法线、材质分组参与几何和法线着色；顶点红通道控制描边宽度。
- 五区颜色、浅影、深影、高光颜色/形状/范围/柔度、金属度/光滑度/强度，
  MatCap 的 tint、mask、alpha/add/overlay、折射、UV 移动与静态时间原参数。
- 原 `_Emission` 开关与五区 `_EmissionColor` 生成实际发光辐射；A.b 为遮罩，
  `_UseMatCapMask` 调整遮罩。按社区 `emission()` 使用 M.b 选择发光颜色。
  发光在路径命中时累加，也能照亮其他表面；目前依赖随机反弹命中，未单独做
  发光纹理区域的直接采样。`--no-emission` 用于纯反射对照。
- 原 rim 的颜色和方向公式进入每次命中的反射响应。原 `_RimWidth` 的偏移深度
  遮罩以每条出射路径自身的视角发射相邻探针射线计算；不再读取首相机深度覆盖图。
- 原 `_Outline`、`_OutlineWidth`、五区颜色和顶点宽度用于可相交的外扩背面壳。
  壳沿 HoyoToon 固定拍摄相机公式展开；每条路径都能命中它，并按暗色材质继续
  反弹，显示阶段不画描边覆盖。

HoyoToon 是社区光栅着色器，原资产没有提供路径追踪 BRDF。本模式明确使用
原方向响应 R 的适配 `f=R/(2π cosθ)`、均匀半球 PDF `1/(2π)`。
直接光与继续反弹共用同一响应，权重 `f cosθ/PDF=R`。
MatCap 在每个路径命中用该条出射方向的视角基采样，作为反射数据乘入积分，
不会直接当作发光或相机成品颜色。所有真实遮挡、多跳、原灯具采样/MIS 和
俄罗斯轮盘赌继续保留。这里的风格化响应及几何依赖的 rim 不宣称物理互易性
或能量守恒，也不声称上述转换与游戏原管线相同。

普通眼睛/眉毛不套用 Eye_E 查色：社区源公式仅在独立 EyeShadow/EyeHighlight
角色使用该 16×16 LUT。导入该网格并显式绑定 role 3/4 与 `eye_color_map` 时
已有路径分支会使用 LUT；本资产普通眼睛没有这些独立网格。
UV2/UV3、UV4+、其他形态与武器没有强行叠到此正常形态静态图上。
`_SecondaryEmission`、`_ScreenImage`、`_UseChannelMixer`、异常状态等原开关为 0
时不擅自打开，空纹理引用也不虚构图片。

PNG 旁 JSON 记录实际加载的纹理文件、完整原属性、缺省字段、实际使用的推断
绑定和拍摄设置。`work/asset-path-v2/data-coverage.json` 记录本轮各通道与原开关，
`uv-inventory.json` 记录原 FBX 的额外 UV 层；清单不能等同于完整原游戏 Shader。
相机、照明、路径积分转换与社区默认值均是明确的实现选择，不冒充解包得到的
原场景参数。CUDA、共享加载器、原 FBX/PNG/材质 JSON 不改写。

```sh
./metal/build.sh
sh ./metal/render_ye_shunguang_asset.sh
python3 metal/validate_asset_sources.py --output ../work/asset-path-v2/checks
```

脚本输出 2048×1710、256 spp、最多八次表面命中。验证通过逐项关闭控制图、
MatCap、法线、SDF、rim、描边和透明度，检查它们对真实 GPU 图像的影响；
关闭全部外部光源与源发光应得到黑色，证明没有相机颜色覆盖。

追踪内核按小块分别提交 GPU 命令，默认每块不超过 1024 像素，
避免驱动因单个命令过长而中断。`--trace-tile-pixels N` 调整预算；
验证中不同分块大小输出逐像素一致。分块不改变全局像素索引、随机状态
或样本数。Metal 管线按着色器内容、GPU 和系统版本缓存到 `build/*.metalar`。

固定相机的离线资产模式预先生成同公式的描边壳并纳入紧密 BVH，
保留 Cull Front、未展开原三角形的排除规则和原顶点属性。
`--dynamic-hulls` 使用原逐次展开方式做几何对照；交互相机仍使用动态展开。


## HoyoToon 代码对齐与 2K 全身图（2026-10-05）

本轮仅修改 Metal。依据 HoyoToon `d9e5ca2f312bf16fba89dee67d32c08b482dcda4` 的 ZZZ `zzz-common.hlsl`、`zzz-program.hlsl` 和 shader 属性默认值。社区复刻公式继续在现有 `R/(2*pi*cos)` 路径适配中计算；直接光、间接光、遮挡、MIS 和俄罗斯轮盘赌继续走光线路径。它不是原游戏的光栅管线或原版 BRDF。

- `prepare_zzz_asset.py` 输出 `ZZZATTR3`：每个顶点保留切线、切线手性、RGBA 顶点色、UV1、脸标记/分区 ID、UV2 和 UV3；OBJ 保留 UV0 与顶点法线。旧 `ZZZATTR1/2` 仍可读取。
- 补齐 HoyoToon `_DoubleUV`、`_DoubleSided`、`_SymmetryUV` 的主贴图/光照贴图 UV 选择；依据其入口直接采样原 UV，未擅自增加源码不存在的 ST 乘法。
- 新包描边按 `_NormalUV`（HoyoToon 默认 UV3）在原始法线/切线坐标中解码，再按顶点色 R 缩放宽度。支持 `_UseLightMapOL`、`_DisableFOVScalingOL`、`_OutlineZOff`。固定相机可预生成相同外扩几何；交互相机实时生成。旧包保留原平滑方向回退。即使原 FBX 的 UV3 复制了 UV0，也不自行改写它来“改善效果”。
- 补齐 `_LegacyOtherData` 的 D.a/N.a/M.a 通道回退和 `_UseLegacyFace` 的入口分支。鼻线/脸部描边颜色按 HoyoToon 三分区选择；身体仍用五分区。
- 轮廓光偏移修正为 `saturate(1/distance)/fov_range`。每次路径命中用邻近射线取得遮挡深度，保留全程路径追踪，而非最后往屏幕上画轮廓光。
- 删除源路径分支中 HoyoToon 函数未乘的 `_BrightMultiplier`；参数仍保存在源报告中。`_ModelSize` 已有正确的五分区读取，保持原实现。

新包位于工作区 `outputs/ye-shunguang-metal-hoyotoon-path`；原 FBX/PNG/JSON 与旧包均不改写。所有源材质浮点、颜色、纹理引用记录在成图同名 JSON 的 `asset_source_report`，记录参数不等于该参数产生可见效果。

使用已绑定的正常形态 D/N/M/A、脸 SDF 和 MatCap 候选；候选纹理的匿名 ID 对应关系仍标明为候选。另一形态、未装备武器、关闭的特效不会混进正常形态图。HoyoToon 自身未使用 `_Anisotropy`、`_NoseLineHoriDisp`、`_NoseLineLkDnDisp`；随后加入的自主设计规则见下节。原鼻线遮罩仍按 HoyoToon `_NoseSmoothX/Y` 默认值计算。额外的 Unity 灯光脚本、LUT/特效绑定和屏幕 Stencil 也不会因材质 JSON 内存在字段就自动恢复。

渲染入口：

```sh
sh metal/render_ye_shunguang_hoyotoon_fullbody.sh
```

输出 2048×2048、256 spp、深度 8 的正面全身图。相机、正面上方方向光和白色环境属于本地渲染设置。

## 自主设计的头发各向异性与动态鼻线（2026-10-05）

仅修改 Metal，在 `--asset-toon` 中默认启用。读取原 JSON 的三项数值，
不改写原材质或贴图。这两项计算规则由我们设计，不是已恢复的官方公式，
也不是 HoyoToon 现成实现。成图 JSON 中的 `authored_hair_anisotropy`、
`authored_dynamic_nose` 明确记录这一点。

- 头发：保留原高光遮罩、强度、颜色和形状。在原切线/副切线方向上，
  把高光半角向量分量分别除以/乘以 `2^(2*A)`，`A` 来自 `_Anisotropy`。
  正值沿模型切线拉长，负值交换长轴；`A=0` 保留原响应。
  只用于头发材质并要求有效的原模型切线；其他材质保持原计算。
  模型 UV 切线不保证就是艺术家另外绘制的发流方向图。
- 鼻线：原 `D.a` 遮罩仍决定哪里可以画鼻线。在头部局部坐标中，
  将 `_NoseLineHoriDisp`、`_NoseLineLkDnDisp` 自主解释为横向与低头方向的
  余弦阈值，并用余弦范围 `阈值±0.05` 平滑淡出。
  当前资产分别为 `0.92`、`0.62`，转到侧面或从上方看时淡出；
  从下方看不触发低头淡出。静态资产用包中的头部世界空间轴，
  将来若旋转头部骨骼，需要同时更新这两个轴。

两项在每次路径命中时使用该条路径的出射方向计算，并同时用于直接光和
继续反弹；没有增加屏幕覆盖、伪发光或最后绘图。采样 PDF、MIS、俄罗斯
轮盘赌与路径深度保持原流程。缺少对应原参数时自动保留原响应。
原高光遮罩可能让真实角色的变化很细微，不强行提高高光强度来制造差异。

对照开关：`--no-anisotropy`、`--no-dynamic-nose`；已有 `--no-nose-line`
优先关闭全部鼻线。小图验证同时检查零各向异性退化、切线方向影响、
两种鼻线方向、真实资产对照和无光源时全黑。

```sh
python3 metal/validate_designed_hair_nose.py --output ../work/metal-designed-hair-nose/checks
```

## HoyoToon 外观匹配：保留全程路径积分（2026-10-05）

这轮仅更改 Metal 文件；所有头文件（包括 `metal/shared_types.h`）、CUDA、共享加载器、原材质、模型和贴图保持原字节。修改前检查了 VS Code 的保存状态：“全部保存”禁用，没有待保存文件。

- 新增 `--sun-angle R`：R 是远处白色圆盘光源的**角半径（度）**。0 保留原 delta 方向光；非零范围 .1–60。按圆锥立体角 `Ω=2π(1−cos R)` 均匀直接采样，光源辐射亮度为 `sun_intensity/Ω`；BSDF 路径逃逸到该圆盘时同样累计光源，并使用匹配 PDF 的 MIS。固定的是角积分辐射亮度，表面照度仍取决于余弦及真实遮挡。没有阴影强度下限、遮挡豁免或屏幕补色。
- 资产模式的倒置描边壳现在采用薄墨层的反射/透射路径响应：`f=R/(2π|cosθ|)`，均匀球面 PDF `1/(4π)`，继续路径权重 `2R`。原颜色公式仍使用原外侧法线，但从壳背面看时也能接收外侧入射光，避免只允许同半球反射造成无光黑线。壳仍参与真实求交、阴影和多跳路径。**这是本地设计的风格化薄层散射模型，不是官方或 HoyoToon 提供的 BSDF，也不宣称整个材质系统满足物理互易性或能量守恒。**
- 新增 `--ground-albedo F`：地面的线性灰色反射率，范围 0–1，默认仍为 .5。只调整真实地面材质，不改变背景或显示结果。
- 参数使用现有 `GPUParams.tone.w` 空闲槽；`.y/.z` 继续保留分块偏移，不修改任何共享布局头文件。

`render_ye_shunguang_hoyotoon_match.sh` 保存一套明确标为人工选择的本地布光：方向 `(0,.15,1)`、角半径 12°、主光 5.35、白色环境 .15、地面反射率 .665、描边全局缩放 .85。拍摄相机保持原 4K 对照设置，关闭自主各向异性与动态鼻线，让这两项回到社区公式。所有原材质参数和候选贴图绑定仍从原文件读取，不改写它们。

这套图**不是原对照的同光照复现**，而是保留路径遮挡、在本地调整真实照明以靠近社区着色器。HoyoToon `_UseSelfShadow` 默认 0、脸部主分支使用 SDF；全路径版本仍然计算实际遮挡，因此封闭区域、颈部和交叠头发不保证与光栅参考逐像素一致。候选匿名纹理映射继续保留其不确定性。

```sh
./metal/build.sh
sh metal/render_ye_shunguang_hoyotoon_match.sh /path/to/metal-refined-4k-256spp.png
# 快速预览；末尾 CLI 覆盖前面的同名选项
WIDTH=768 HEIGHT=432 sh metal/render_ye_shunguang_hoyotoon_match.sh /tmp/match-preview.png --frames 4
python3 metal/validate_hoyotoon_refinement.py --output /tmp/hoyotoon-refinement-check
```

数值验证用独立 Lambert 解析积分检查 0/8/16/24° 光源，比较直接估计与带 BSDF 命中的 MIS，并检查真实遮挡板、零光源、线性光照缩放、分块一致性和深度 1/8 的实际资产图像。`validate_asset_sources.py` 继续检查来源优先级、28 张贴图、逐项材质开关与零光源。PNG 元数据记录光源角宽、地面反射率、描边散射规则和真实路径模式。


### Screenshot reference: authored static pose

`prepare_zzz_asset.py --pose ye_shunguang_reference_pose.json` bakes FBX cluster skinning for an authored standing pose with lowered arms, curled right fingers and one rigidly placed source weapon. This is not recovered game animation. UV/color channels remain original; deformed normals and tangent frames are exported for lighting and geometry outlines. Original material files remain read only. Optional `authored_floats`/`authored_colors` override selected source controls and are reported separately from the original parameters.

`sh render_ye_shunguang_reference.sh POSED_ASSET_DIR OUTPUT.png` renders a portrait at 1800×2400, 256 spp, depth 8. Set WIDTH/HEIGHT/FRAMES to adjust quality. This preset uses a finite distant light, white environment and no ground; all character shading remains path integrated. `--no-ground` affects scene geometry only. Its lighting, highlighter suppression and pose values are local visual choices. The shader integrator is unchanged from the preceding HoyoToon path implementation.
