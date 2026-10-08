# 最新蕾米 1K/64 spp 任务副本

2026-10-08 单 RTX 5090 实际任务的源码副本：DLSS UltraPerformance，SDK 返回 1008×655 → 3024×1964，64 spp，120 fps，12 秒。常规 `reconstruction/` 保留 Quality 模式，两者没有自动同步。

这里的 prepare.py/build.sh 必须显式设置 PORT_ROOT 指向仓库 `nvidia_port`，不能用当前位置推断。依赖 NVIDIA 官方 OptiX 9.0、CUDA 12.8 或支持 Blackwell 的 CUDA Toolkit、DLSS SDK 310.5.3、Linux Clang 主机编译器。SDK 不含在提交里。

run_job.py 读取已准备的 `job/cloud_render.py` 与 scene-3024x1964.npt，设置 NPT_ROOT 为完整数据包解压根路径，NPT_BINARY/NPT_PTX 为构建结果，NPT_RECONSTRUCTION=dlss，NPT_WORKERS=1，NPT_SPP=64，NPT_OUTPUT_DIR 为不同的新目录。源码仍保存当时尝试的 hevc_nvenc 编码配置；该容器失败后改用外部 CPU libx264 veryfast CRF17 编码，因此不能保证原启动脚本在另一容器上完成编码。

不提供自动租机、开机或关机脚本，避免在另一设备误操作实例。详见仓库 WINDOWS_HANDOFF.md。
