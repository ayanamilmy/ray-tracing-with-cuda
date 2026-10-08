# 独立 CUDA/OptiX 重建后端

这个目录添加 NVIDIA CUDA DLSS Ray Reconstruction（降噪与超分）及 OptiX HDR 空间降噪。
光照仍由原有路径积分器计算，辅助数据也用射线查询生成。DLSS 不生成动画帧，不改变原始动作时间轴。
经过 AI 重建的最终像素会改变，不能把它称为未经处理的原生路径追踪结果。

当前正在运行的蕾米埃尔成片仍使用原程序、3024×1964、120 fps、256 spp。
此功能没有替换正式任务。源码全部新增在本目录；原 CUDA、公共头文件及 Metal 源文件没有修改。

## 模式

| NPT_RECONSTRUCTION | 行为 |
|---|---|
| off（默认） | 原始输出，不抖动相机，不改变 RNG；可用于逐像素回归 |
| optix | 同分辨率 OptiX HDR 空间降噪；没有跨帧历史 |
| dlss | CUDA DLSS RR；同分辨率使用 DLAA，放大使用 Quality；不叠加 OptiX 降噪 |

`prepare.py` 复制现有独立后端的所需文件到新构建目录，并记录原文件 SHA256。
只在这些生成副本中扩展私有 Launch 布局、增加辅助射线程序、接入重建和输出尺寸。
`reconstruction.cu` 不包含你手写的原 CUDA 或公共头文件。

## 构建

已验证环境：Linux x86_64、RTX 5090、驱动 595.71.05、OptiX 9.0、DLSS SDK 310.5.3。
需要 NVIDIA CUDA、clang++、OptiX 9.0 头文件和官方 DLSS SDK。这里没有把 NVIDIA SDK 签入源码。

```bash
export PORT_ROOT=/path/to/nvidia_port
export OPTIX_INCLUDE=/path/to/optix-dev-9.0.0/include
export DLSS_ROOT=/path/to/DLSS-310.5.3
export BUILD_DIR=/path/to/separate-reconstruction-build
bash /path/to/nvidia_port/reconstruction/build.sh
```

编译产物是 `optix_sequence_reconstruction`、`device.ptx`、`libnvidia-ngx-dlssd.so`。
保留运行库与程序在同一目录。第一次启动 OptiX 会编译并缓存新着色器，可能等待数分钟。

查询 SDK 推荐的 Quality 输入尺寸：

```bash
"$BUILD_DIR/optix_sequence_reconstruction" --dlss-optimal-size 3024 1964
```

本次 SDK 实际返回 **2016×1309 → 3024×1964**，不是猜测出的官方游戏参数。

## 后续动画入口

`run_job.py` 适配目前已准备的三页动画任务格式（`job/cloud_render.py`、`scene-3024x1964.npt`、`timeline.py` 等）。
它读取原任务，另存输入场景，在内存中适配驱动；不改写原任务源码。
必须指定新结果目录，程序拒绝写入原任务的结果目录及其子目录。

```bash
export NPT_BINARY="$BUILD_DIR/optix_sequence_reconstruction"
export NPT_PTX="$BUILD_DIR/device.ptx"
export NPT_RECONSTRUCTION=dlss
export NPT_OUTPUT_DIR=/path/to/new-results
export NPT_OUTPUT_WIDTH=3024 NPT_OUTPUT_HEIGHT=1964
export NPT_SPP=256 NPT_WORKERS=2

python run_job.py /path/to/prepared-job/job/cloud_render.py 0
# 另一个终端运行 GPU 1：
python run_job.py /path/to/prepared-job/job/cloud_render.py 1
```

默认仍为 256 spp；此处没有凭低清测试决定正式成片应降低到多少采样。
`NPT_SPP` 可设为正的 16 倍数或 1、4、9；`NPT_WORKERS=1` 时只运行 GPU 0。
`--benchmark` 配合 `NPT_BENCH_FRAMES=248` 只渲染指定帧；`--dry-run` 只显示分配结果，不渲染。
`NPT_RESET_FRAMES` 是逗号分隔的明确切镜头帧号。
本入口不自动安排关机；后续正式任务需要沿用完成验证后的关机监控。

DLSS 双卡使用连续时间段。第二卡及断点续跑先在独立 warmup 目录渲染最多 32 帧历史，
避免把两套奇偶帧历史交错输出。缺帧跨度与显式切镜头发送 `RESET`；预热不覆盖已经保存的正式帧。
直接使用后端时，mmap/file 命令支持 `输入路径 TAB 输出路径 TAB RESET`。
直接使用后端还须按其实际帧间隔设置 `NPT_FRAME_DELTA_MS`，默认 1000/120 毫秒。

## 数据与边界

- 输入为曝光与色调映射前的线性路径追踪积累结果；曝光、色调映射、sRGB 编码仍在原显示阶段。
- 辅助缓冲包括底色、世界空间着色法线、线性相机深度、粗糙度、镜面底色和运动矢量。
- 三角形用当前命中重心坐标找到前一帧同一表面点，结合两帧真实相机计算当前到前帧的像素位移。
- 矩阵按官方 RR 指南采用行主序；底部向上的原缓冲通过 NGX 的 Y 轴指示与原显示翻转处理。
- DLSS 模式使用 Halton 相机抖动，并在第一遍路径采样前初始化逐帧 RNG，避免重复噪声。
- NGX 调用前等待输入纹理完成，调用后等待该 CUDA 上下文内的工作完成再读输出，避免跨流读取黑帧/旧帧。
- Source Toon 材质不是官方 PBR 材质。辅助数据取现有 `mmd_base`/`character_bsdf` 的底色；
  Source 分支采用粗糙度 1、镜面底色 0 的重建提示。这是本实现的近似，不是解包出的官方粗糙度。
- 透明表面的辅助可见性沿用现有深度程序的 alpha 0.5 判定；它不等于所有随机透明路径的命中。
- 尚未实现镜面二次命中距离/镜面运动、透明覆盖层或景深专用引导。复杂镜面、玻璃、透明头发与细线条仍需单独验证。
- 本次样片不能证明长片完全无拖影或与原生 3K、256 spp 等质；正式降低采样前还应对同帧、同机位做画质对照。

## 已完成验证

1. NGX CUDA 能力查询返回 DLSS RR 可用，实际创建与执行成功。
2. 原生模式三帧（60、61、62）与旧程序已渲染预览逐像素一致，最大像素差为 0。
3. OptiX 同分辨率降噪输出成功。
4. 三种机位各一秒，累计 90 帧、30 fps、16 spp，504×328 输入重建至 756×492，含跨段历史重置；同步修正后九十帧均有正常图像。
5. 使用正式任务格式的独立启动入口实际输出 3024×1964 图像，输入为 SDK 推荐的 2016×1309。
6. 检查 31 个受保护原 CUDA/头文件 SHA256，全部未变。

最终三秒样片辅助数据加重建耗时中位数约 **12.17 ms**。
3K、16 spp 单帧实测：路径阶段约 **2.35–2.38 s**，辅助数据加重建约 **16–19 ms**。
这些测试与正式渲染同时进行，不是独占显卡基准；不能直接推算完整版总时长或相同画质的提速比例。

## 官方依据

- [NVIDIA OptiX/CUDA DLSS RR 示例](https://github.com/NVIDIA/optix-subd)
- [官方 CUDA 接入实现](https://github.com/NVIDIA/optix-subd/blob/main/denoiserDlss.cu)
- [DLSS SDK 310.5.3](https://github.com/NVIDIA/DLSS/tree/v310.5.3)
- [DLSS RR Integration Guide](https://github.com/NVIDIA/DLSS/blob/v310.5.3/doc/DLSS-RR%20Integration%20Guide.pdf)
