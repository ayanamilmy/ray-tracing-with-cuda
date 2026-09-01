# CUDA 光线追踪(Ray Tracing in One Weekend 的 GPU 移植)

从零学 CUDA 的项目:把《Ray Tracing in One Weekend》逐步移植到 GPU,带交互窗口。
当前版本:M4 —— 自由相机(UE5 式操作)+ 静止累计采样 + 双边滤波降噪。

## 环境要求

- Windows 10/11
- Visual Studio 2022(需要它的 C++ 工具链,编译脚本靠 vcvars64.bat 配置 MSVC 环境)
- CUDA Toolkit 13.1(12.x 应该也行)
- NVIDIA 显卡(默认按 sm_89 编译 = RTX 40 系,其他显卡见下)

## 编译

仓库中自带编译脚本 `build.bat`,用法:

    build.bat [源文件] [输出 exe] [额外 nvcc 参数...]

编译当前主力版本:

    build.bat main_accum.cu accum.exe

不带参数 = 编译 main_accum.cu → accum.exe。

**两个按自己机器改的地方:**

1. vcvars64.bat 路径:build.bat 里写死了 VS2022 默认安装路径,装别处就改这一行:
   `call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"`
2. 显卡算力:build.bat 里是 `-arch=sm_89`(RTX 40 系):

| 显卡 | -arch |
|------|-------|
| RTX 40 系 (Ada) | sm_89 |
| RTX 30 系 (Ampere) | sm_86 |
| RTX 20 系 (Turing) | sm_75 |

## 文件入口

| 文件 | 是什么 |
|------|--------|
| `main_accum.cu` | **主力版本**(M4):交互窗口,自由相机 + 累计采样 + 双边滤波 |
| `main_anim.cu` | 早期动画版:固定机位绕圈 + 盒式模糊 |
| `main.cu` | 单帧渲染版,输出 PPM 文件 |
| `lvbo.cu` | 双边滤波练习(灰度版) |
| `myself.cu` / `selfi.cu` | 早期练习 |
| `*_backup_*` / `*_progressive` | 学习阶梯的历史备份,不重要 |

渲染核心都在 `.h` 头文件里:vec3 / ray / sphere / bvh / material / camera。

## 运行

    accum.exe                → 720×360,默认参数
    accum.exe 16 0 0         → 16 spp、关滤波
    accum.exe 4 3            → 播 3 秒自动退出,控制台打印平均帧率
    accum.exe 4 0 0.15 1280 720 → 1280×720 大图

参数:[spp] [秒数,0=一直播] [sigma_r 颜色容忍度,0=关] [宽度] [高度]

窗口操作:右键旋转 / 中键平移 / 滚轮缩放 / F 聚焦 / 按住右键 + WASD 飞行 / SPACE 暂停攒样本 / ESC 退出。这个是仿照UE5来写的。
