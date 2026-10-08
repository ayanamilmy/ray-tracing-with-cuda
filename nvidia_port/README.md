# 独立 NVIDIA 路径追踪移植

新增目录：`ray-tracing-with-cuda/nvidia_port/`。原项目根目录的 CUDA、公共头文件、Metal 源码和构建入口全部保持原样。本目录复制并适配 2026-10-06 当前 Metal 实现，有自己的类型、材质、向量数学、BVH、资源读取和构建入口，不 include 原 CUDA 类，也不依赖原公共头文件。

这是我维护的移植版本，与你自己编写的 CUDA 学习版本分离。删除或移动这个文件夹不会改变原版本。保留的 `snapshot/` 是审计用的原始 Metal 源码快照；构建不会修改或同步原项目。

## 实现范围

- 迁移原 Metal 的完整路径积分、XORWOW 随机数、纹理过滤、法线与四套 UV、材质分区、脸部 SDF、头发/衣服角度响应、透明度、路径描边、太阳面积采样与 MIS。
- 独立 NVIDIA CUDA 内核逐像素执行相同采样逻辑，支持离线累计和分块调度；`src/render.cu` 保留软件 BVH 基线。新增的 `optix/` 后端使用 OptiX / RT Core 加速求交，见 [OptiX 使用说明](optix/README.md)。
- 仅允许 source-material 完整路径追踪场景。载入器拒绝旧 Toon 合成模式、屏幕描边、头发叠加、双边滤波等配置。描边使用打包的几何薄壳，参与路径和阴影射线。
- HoyoToon 角度响应到 BSDF 的数学适配沿用原 Metal 实现；这是本项目的适配，不声称是解包得到的官方 BRDF。官方资产数据和本项目缺省/推测绑定的区别保留在包的 `.json` 报告中。
- 动画使用已烘焙的原始动作帧：骨骼、表情、武器和服饰通过已有动画烘焙流程生成 OBJ/属性包，本后端读取它们。不在云端重新解包游戏，不需要下载几十 GB 游戏数据。

## 文件结构

```text
nvidia_port/
  src/render.cu           独立 NVIDIA 执行、显存、调度、输出入口
  src/cpu_reference.cpp   同一套像素内核的 CPU 验证入口
  src/pack_scene.mm       Mac 场景打包入口（无需 Metal GPU）
  include/pathtracer.h    从固定 Metal 快照转换的完整计算逻辑
  include/vector_math.h          自有 Clang 向量/数学适配
  include/types.h         自有 GPU 布局，尺寸/偏移断言
  include/scene_io.h      独立二进制读取、范围检查、PPM/线性输出
  vendor/                资源读取/第三方头文件独立副本，保留许可
  snapshot/              Metal 原始快照与 SHA-256 来源记录
  tools/translate.py      可复现的 Metal → Clang CUDA 转换
  tools/compare_images.py 画面误差对照
  tools/check_isolation.py 原文件哈希检查
  build_mac.sh           构建打包器与 CPU 验证程序
  build_cuda.sh          Linux NVIDIA 的独立构建入口
  pack_entry_frame.sh    打包入口动作帧，沿用上一张渲染的设置
  optix/                独立 RT Core 后端、预编译 PTX、云端预检和对照脚本
```

## 编译器选择与当前验证状态

该移植使用 **Clang CUDA**，仍在 NVIDIA GPU 上执行 CUDA 程序；为了直接保留 Metal 向量与 swizzle 语义，当前不支持直接用 `nvcc` 编译。不会影响你的原 CUDA 项目使用 nvcc。Linux 云机需要带 NVPTX 后端的 LLVM Clang、CUDA Toolkit 和 NVIDIA 驱动。

本机验证：Apple Clang 21.0.0 / ARM64；NVIDIA 官方 CUDA 12.8.90 runtime/CCCL、12.8.93 nvcc 包内的头文件、cuRAND 10.3.7.77 头文件（仅下载到任务临时目录）。

- Mac 打包器、CPU 验证版构建成功。
- CUDA 主机端与 sm_89 GPU 端语法/语义检查成功；GPU 端 LLVM IR 生成成功。
- Clang 对 CUDA 12.8 显示“仅部分支持”的兼容性警告。
- 本机 Apple Clang **没有 NVPTX 代码生成后端**，也没有 NVIDIA GPU，因此完整 PTX/机器码生成、CUDA 链接和 NVIDIA 上机运行尚未验证。此目录是已通过本地对照的移植源码，不能据此宣称云端已可用或某个加速倍数。
- AddressSanitizer + UndefinedBehaviorSanitizer 渲染第 105 帧 240×320、16 spp，无报告错误。
- 第 105 帧相同参数的 CPU/Metal 对照：16 spp 完全相同像素约 99.967%；256 spp 约 99.883%。256 spp 平均通道绝对差 0.001042 / 255，最大通道差 7。GPU/CPU 数值差异可能影响少数路径，这里只报告测量结果，不认定所有差异的成因。
- 92 个已有文件 SHA-256 核对全部相同；没有新增文件落在 `nvidia_port/` 之外。
- 负向验证：启用头发叠加的测试场景被读取器拒绝，未生成图像。

## Mac 使用

### 新增 RT Core 验证（2026-10-06）

`optix/` 后端使用固定的 OptiX 9.0 头文件；已在 Mac 上用 Homebrew LLVM 21.1.8 成功生成 `sm_120` PTX，并提供校验源码哈希的预编译部署方式。生成的纯模式 CPU 入口与此前第 105 帧 256 spp 基线逐字节一致；实际资产 177,720 次正反面属性重建检查通过，ASan/UBSan 无报告错误。NVIDIA 上机图像、Linux 链接和性能仍待验证。详见 [OptiX 使用说明](optix/README.md)。

原 `src/render.cu` 软件版本保持原样；新增后端内联向量辅助函数，处理完整 NVPTX 生成时发现的 LLVM 向量代码生成问题。该问题仅用前端语法/IR 检查无法发现。首次云端对照可在同一个 OptiX 模块中切换软件 BVH 与 RT Core。

### 本地打包与 CPU 对照

在此目录运行：

```sh
sh build_mac.sh
sh pack_entry_frame.sh /absolute/path/to/posed/asset /absolute/path/frame105.npt 1800 2400
build/cpu_reference /absolute/path/frame105.npt /absolute/path/cpu.ppm 16 8
```

`asset` 是已有输出 `ye-shunguang-entry-frame/asset/`，里面有 `character.obj`、材质和属性文件。打包器会读取该资源包所引用的贴图，将所有像素、材质、几何和 BVH 放入同一个 `.npt`，云端读取时无需访问 Mac 的绝对路径。

每轮 16 spp，16 轮累计即 256 spp。最后一个 `8` 是 CPU 线程数。CPU 验证版用于对照，不能代表 NVIDIA 性能。

`pack_entry_frame.sh` 的相机、灯光、描边等是上一张项目渲染设置，不是官方参数。新动作帧需要先运行原动画烘焙工具生成另一份 `asset/`，然后打包。

## Linux / NVIDIA 云机使用（待上机验证）

上传本目录和 `.npt` 包，云机安装好完整工具链后：

```sh
CUDA_CLANG=clang++-21 CUDA_TOOLKIT=/usr/local/cuda CUDA_ARCH=sm_89 sh build_cuda.sh
build/nvidia_render /absolute/path/frame105.npt /absolute/path/frame105.ppm 16
```

RTX 3090 选择 `sm_86`；RTX 4090 选择 `sm_89`。默认每块 32768 像素，输出 256 spp 的 PPM 和时间/设备 JSON 报告；可选第五个参数导出 float4 线性数据：

```sh
build/nvidia_render scene.npt image.ppm 16 32768 image.f32
```

PPM 可用 Pillow 转 PNG，或 FFmpeg 合成视频。程序不会安装依赖、申请云机器、购买服务或上传文件。实际价格/时间以云机测试为准，先测小图再做完整动画。

运行多个 GPU 时每台负责独立帧；本程序单进程选 GPU 0，整个动画的云端任务分发尚未加入。

打包的第 105 帧小图场景未压缩约 305 MB，其中纹理像素约 262 MB；分辨率变化基本不改变场景包大小。可先 gzip 再上传。已有解码的动作数据可以复用，无需上传原始游戏 Blocks。

## 复现与原文件保护

```sh
python3 tools/translate.py
python3 tools/check_isolation.py
```

`check_isolation.py` 只读原仓库文件；在完整原仓库中运行，若你以后自行修改原版本，它会据实报告差异。孤立上传到云机后不应运行此检查，因为原仓库不在云机。

第三方许可保留在 `vendor/` 中及头文件顶部。来源摘要在 `snapshot/provenance.json`。

Clang CUDA 构建参考：https://llvm.org/docs/CompileCudaWithLLVM.html
