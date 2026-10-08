# Windows 接手说明（2026-10-08）

本分支保存当前磁盘上的渲染器源码。根目录 CUDA/公共头文件是用户已有代码的保存状态；此次上传没有改写它们。源代码关系为 `main_accum.cu → Metal 实现 → 固定 Metal 快照转换出的独立 NVIDIA 后端`，随后增加 OptiX RT Core、动画传输及 DLSS 重建。独立 NVIDIA 版构建不 include 原根目录 CUDA/公共头文件。

## 获取

```powershell
git clone --branch codex/windows-renderer-handoff-20261008 https://github.com/ayanamilmy/ray-tracing-with-cuda.git
cd ray-tracing-with-cuda
```

- 原学习版：根目录 `main*.cu`、`*.h`、`build.bat`。已有 Windows/nvcc 入口。
- Metal：`metal/`，只能在 macOS 使用。
- 独立 CUDA：`nvidia_port/src/`、`include/`。
- RT Core：`nvidia_port/optix/`。
- 常规 Quality DLSS：`nvidia_port/reconstruction/`。
- 最新单 5090、1K/64 spp 成片使用的代码副本：`nvidia_port/variants/remielle_dlss64/`。该副本使用 UltraPerformance，不能把常规 Quality 后端误当成最新任务代码。
- 三页动画的姿势、时间线和驱动：`nvidia_port/examples/remielle_three_pages/job/`。

## 当前平台边界

独立后端当前已验证 Linux x86_64 + RTX 5090。构建脚本包含 Linux CUDA/NGX 动态库及 Clang 向量类型，场景打包器含 Cocoa/simd；没有完成 Windows 原生移植。最直接的已验证流程是：在 Windows 编辑源码，通过 SSH 上传到 Linux NVIDIA 实例编译、渲染、取回结果。WSL2 本机 OptiX/DLSS 能否使用仍需实际验证，不能保证。

构建源码生成与重建后端可按以下顺序进行，SDK 使用各 README 中固定版本：

```bash
python3 nvidia_port/optix/restore_nvcc_source.py
export PORT_ROOT="$(pwd)/nvidia_port"
export OPTIX_INCLUDE=/path/to/optix-dev-9.0.0/include
export DLSS_ROOT=/path/to/DLSS-310.5.3
export BUILD_DIR=/path/to/separate-build
bash nvidia_port/variants/remielle_dlss64/build.sh
```

最新任务推荐尺寸由 SDK 查询得到：1008×655 输入 → 3024×1964 输出，64 spp、深度 8、120 fps、12 秒、1440 个真实动画帧。无 DLSS 插帧。连续帧使用连续时间段与历史预热。

完整的模型、贴图、动作、相机字段和 .npt 场景不在 Git 历史中。已有 `remielle-three-pages-cloud.tar.gz`（约 92 MB）需要另行复制到 Windows/云端。它解压后包含准备好的数据与场景；例子 job 只提供源代码，不能凭源代码单独重建原始资产。

## 最新任务记录

全部帧已完成，成片已下载回 Mac。原视频在 `outputs/remielle-dlss64/final/`，3024×1964、120 fps、12 秒、约 32 MB。另有附 QuickTime 正常速率标记的 MOV。它们不包含在代码提交里。

RTX 路径追踪与 DLSS 正常执行。该实例 FFmpeg 的 NVENC 编码报 unsupported device，最后使用 CPU libx264、veryfast、CRF 17 编码；当前保存的 run_job.py 仍记录当时 NVENC 入口，重新跑时必须按实际容器编码能力调整。

监控必须同时验证非空视频、ffprobe 尺寸/帧率/帧数/时长。不能仅检索 VIDEO_DONE 子串（异常栈也可能包含该字符串），不能把 SSH 断线视为完成。下载器的 Unix ControlPath 应短于系统限制。密码、SSH socket、SDK 二进制、云端连接配置未上传。

相机与材质适配有官方资源字段和本项目推断之分；仍未确认的运行时转换/插值，不应称为完整官方实现。
