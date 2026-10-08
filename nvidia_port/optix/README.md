# 独立 OptiX / RT Core 后端

本目录只扩展 `nvidia_port/`。原项目的 CUDA、公共头文件和 Metal 实现不参与修改。

角色表面和已烘焙的描边薄壳使用 OptiX 内建三角形相交与硬件加速结构；球形光源使用自定义解析求交。每条路径的直接光采样、BSDF、MIS、俄罗斯轮盘、多次反弹、纹理、脸部 SDF 和随机数仍由已有积分器计算。没有新增光栅化、屏幕描边、插帧或 AI 降噪。

透明头发依然通过原积分器按距离查询下一层表面，再进行随机透明度判断；阴影透射也继续按最近交点累乘。any-hit 只做材质分层、正反面和描边接受规则，不消耗随机数。三角形属性来自硬件重心坐标，按原代码重建 UV 和法线。

`make_kernel.py` 读取独立移植版本的 `include/pathtracer.h`，将 `world_hit` 分派到 OptiX，并保留软件求交供验证；渲染入口固定为读取器已经强制要求的纯路径模式，剔除不可达的旧合成入口。路径积分、BSDF、透明度、随机数和显示计算不变；生成文件落在 `build/optix/`。不会修改积分器源文件或原 Metal 快照。设备端内联向量辅助函数，以避免 LLVM NVPTX 的向量代码生成问题，两个求交入口保留为独立函数。

## 构建和第一次测试

需要 Linux、CUDA Toolkit 12.8、LLVM Clang 21（含 NVPTX）、OptiX 9.0 和 NVIDIA R570 或更新驱动。默认目标是 RTX 5090 的 `sm_120`。OptiX 9.0 固定版本头文件从 NVIDIA 官方 GitHub 获取并核对 SHA-256，下载到本目录 `build/`，不会安装系统驱动。

首次部署可以复用交付包中的预编译 `sm_120` PTX，只需普通 Clang（建议 16 或更新）、CUDA 头文件/库及 OptiX 驱动，无需在云端安装 LLVM 21：

```sh
sh optix/preflight.sh host
sh optix/build_host.sh
sh optix/benchmark.sh /absolute/path/frame105-240x320.npt /absolute/path/benchmark 16
```

`use_prebuilt.py` 校验 PTX 与所有设备源码输入的 SHA-256；源码变动会拒绝复用旧 PTX。下列命令用于从设备源码完整重编。

```sh
sh optix/preflight.sh
sh optix/build.sh
sh optix/benchmark.sh /absolute/path/frame105-240x320.npt /absolute/path/benchmark 16
```

`benchmark.sh` 先用 16 spp 的小图逐条校对路径中的相交查询，再在同一个 OptiX raygen/编译模块中分别运行软件 BVH 与 RT Core，各 256 spp。输出交点计数、画面误差、GPU 信息、构建时间和纯渲染时间；不是只比较两套不同编译器的运行速度。

若校对有差异，验证程序保留图像和 JSON，以退出码 3 停止后续测试。应先检查差异再运行大图。硬件三角形精度、共面/重合表面和 BVH 遍历顺序可能改变少数路径；现有描边规则还会根据先前最近交点判断原三角形遮挡，因此不能承诺逐像素完全一致。

普通渲染：

```sh
build/optix_render build/optix/device.ptx scene.npt image.ppm 32 32768 image.f32
```

源场景每轮 16 spp 时，32 轮为 512 spp，64 轮为 1024 spp。输出线性 float4 是可选参数，可用 `-` 跳过。最后添加 `verify` 进行软件交点对照，或 `software` 使用软件 BVH 基线；正常渲染不要启用这两个模式。

动画每一帧读取已烘焙几何并重建加速结构，目前采用逐帧独立进程；没有将全段动画都缓存到显存，也没有完成跨帧 GAS refit。120 fps 的骨骼/表情重采样仍需要动画导出流程另外支持。

## 本机验证与边界

Mac 可以校验几何、生成 PTX、检查主机代码，但无法加载 NVIDIA 的 OptiX 驱动并执行 RT Core。以交付验证报告为准：本地成功编译不等于云端已实测。

本次使用 Homebrew 官方 LLVM 21.1.8（仅解压到任务临时目录）成功生成 PTX 8.7 / `sm_120`。主机 C++ 语法检查通过。实际角色与描边壳的 177,720 个正反面属性重建对照通过，ASan/UBSan 无报告错误。生成入口的 CPU 执行与此前 CPU 基线在第 105 帧 240×320、256 spp 的输出逐字节一致。尚未验证 Linux 链接、NVIDIA 驱动加载、GPU 图像或速度；Clang 仍提示 CUDA 12.8 仅部分支持。

```sh
python3 optix/make_kernel.py
clang++ -std=c++20 -O2 -fno-fast-math -ffp-contract=off optix/check_geometry.cpp -o build/optix/check_geometry
build/optix/check_geometry scene.npt
python3 tools/check_isolation.py
```

交点距离验证容差为 `2e-4 * max(1, abs(t))`，UV 容差为 `2e-4`，同时核对材质、描边标记、正反面和法线夹角。它覆盖实际被路径积分器发出的主射线、反弹、阴影和其他辅助查询；共边材质分界和几何重合需要单独看图判断，不能仅靠误差统计批准整段渲染。

来源：[NVIDIA OptiX 9.0 头文件及驱动要求](https://github.com/NVIDIA/optix-dev/releases/tag/v9.0.0)、[OptiX 概述](https://developer.nvidia.com/rtx/ray-tracing/optix)。
