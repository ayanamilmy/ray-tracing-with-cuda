# CUDA C++ 编程指南 2.1 节「Intro to CUDA C++」逐节详解 × 你的光线追踪项目复习

> 本文档 = 官方文档 2.1 节(2.1.1 ~ 2.1.10)逐段讲解 + 你项目里每一个文件的对应复习。
> 文档中每个知识点都标注了「📌 你的项目」;文末有两张总对照表,保证两边零遗漏。

## 目录

1. [开头先把整体画面拼起来](#1-开头先把整体画面拼起来)
2. [2.1 导言:Runtime API 是什么](#2-21-导言runtime-api-是什么)
3. [2.1.1 用 NVCC 编译](#3-211-用-nvcc-编译)
4. [2.1.2 内核(Kernels)](#4-212-内核kernels)
5. [2.1.2.3 线程与网格索引内建变量](#5-2123-线程与网格索引内建变量)
6. [2.1.2.3.1 越界检查](#6-21231-越界检查)
7. [2.1.3 内存:统一内存 vs 显式内存](#7-213-内存统一内存-vs-显式内存)
8. [2.1.4 同步 CPU 和 GPU](#8-214-同步-cpu-和-gpu)
9. [2.1.5 综合示例 + __syncthreads](#9-215-综合示例--__syncthreads)
10. [2.1.6 运行时初始化](#10-216-运行时初始化)
11. [2.1.7 错误检查](#11-217-错误检查)
12. [2.1.8 设备与主机函数](#12-218-设备与主机函数)
13. [2.1.9 变量说明符](#13-219-变量说明符)
14. [2.1.10 线程块簇](#14-2110-线程块簇)
15. [项目逐文件深度复习(文档 2.1 没讲的知识点)](#15-项目逐文件深度复习)
16. [两张总对照表(零遗漏核对)](#16-两张总对照表)
17. [附录:编译命令、改进建议](#17-附录编译命令改进建议)

---

## 1. 开头先把整体画面拼起来

你的项目做的事情,一句话:**一个像素 = 一个 GPU 线程,每个线程发射一条光线,算出颜色写回数组**。整体架构:

```
┌─────────── 主机 (CPU, main.cu 的 main()) ──────────┐      ┌──────────── 设备 (GPU) ────────────┐
│                                                    │      │                                    │
│  cudaMallocManaged(&fb, ...)   ←── 统一内存 ───────┼──────┼─▶ fb: 每像素一个 vec3 (GPU写 CPU读)  │
│  cudaMalloc(&d_rand_state,...)                    │      │    d_rand_state: 每像素随机种子     │
│  cudaMalloc(&d_list / &d_world)                   │      │    d_list/d_world: 场景指针(仅GPU用)│
│                                                    │      │                                    │
│  create_world<<<1,1>>>(d_list, d_world) ────启动───┼──────┼─▶ 在显存里 new 出 2 个球 + 世界列表  │
│  render_init<<<blocks,threads>>>(...)    ────启动───┼──────┼─▶ 50 万个线程各自初始化随机种子     │
│  render<<<blocks,threads>>>(fb, ...)     ────启动───┼──────┼─▶ 50 万个线程各自发射光线算颜色     │
│  cudaDeviceSynchronize()        ◀── 等待 GPU 完成 ─┼──────┘                                    │
│  打印 PPM 图像(读 fb)                              │                                           │
│  cudaFree(fb)                                      │                                           │
└────────────────────────────────────────────────────┘
```

你写在 [main.cu:10-14](main.cu#L10-L14) 的三条前置知识注释,正对应这套架构:
1. 照片分宽(nx=1000,横)和高(ny=500,竖);
2. 宽和高分别分线程(二维 grid/block);
3. 颜色用 vec3 实现(一个 vec3 就是一个 RGB 像素)。

官方文档 2.1 节就是这套架构的**语言基础**:怎么定义 kernel、怎么启动、线程编号怎么算、内存放哪、怎么同步。

---

## 2. 2.1 导言:Runtime API 是什么

文档原文要点:
- 本章通过 C++ 展示 CUDA 编程模型的基本概念。
- 主要讲 **CUDA Runtime API**:它是 C++ 里最常用的 CUDA 使用方式,构建在更底层的 **CUDA Driver API** 之上。
- 文档假设你已装好 CUDA Toolkit、NVIDIA 驱动,并且有一块受支持的 NVIDIA GPU。

**意思解读**:CUDA 有两套接口。Driver API(`cuInit`、`cuCtxCreate`、`cuLaunchKernel` 等 `cu` 前缀函数)最底层、最啰嗦;Runtime API(`cudaMalloc`、`<<<>>>` 启动语法等 `cuda` 前缀函数)在 Driver API 之上封装了上下文管理、模块加载等苦活,你只需写业务逻辑。两套 API 还能混合使用(文档指向了互操作章节)。

📌 **你的项目**:从头到尾只用了 Runtime API——`cudaMallocManaged`、`cudaMalloc`、`cudaDeviceSynchronize`、`cudaFree`、三重尖括号 `<<<>>>`。你从未手动创建 context,因为它会隐式完成(见 [第 10 节](#10-216-运行时初始化))。

---

## 3. 2.1.1 用 NVCC 编译

文档原文要点:
- GPU 代码用 **nvcc**(NVIDIA CUDA Compiler)编译。
- nvcc 是 **compiler driver(编译器驱动)**:它自己不直接生成机器码,而是提供命令行选项,**调用一整套工具链**完成各个编译阶段。

**意思解读**:`nvcc main.cu` 背后实际发生的事:

```
main.cu ──预处理/分离──▶ 主机代码部分 ──▶ 宿主编译器 (Windows: MSVC cl.exe / Linux: g++) ──▶ CPU 机器码
                        └─▶ 设备代码部分 ──▶ cicc ──▶ PTX/中间码 ──▶ ptxas ──▶ cubin(GPU 机器码)
最后链接器把两者 + CUDA 运行时库 (cudart) 拼成一个可执行文件。
```

- `.cu` 扩展名就是"这个文件里混着主机代码和设备代码"的标志。
- Windows 上 nvcc 要求安装 MSVC(cl.exe)作为宿主编译器;**MinGW 的 g++ 不能作为 nvcc 的宿主编译器**。

📌 **你的项目**:
- 你的所有代码都通过 `main.cu` 一个编译单元编译(其余是头文件被 include 进来),命令形如:
  ```
  nvcc main.cu -o main.exe
  ```
- 注意 [.vscode/tasks.json](.vscode/tasks.json) 里的默认构建任务是 `g++.exe`——**这个任务编译不了 .cu 文件**!你现在的 `main.exe` 应该是你在终端里用 nvcc 编译的。建议把 tasks.json 换成 nvcc 任务(见 [附录](#17-附录编译命令改进建议))。
- [.vscode/c_cpp_properties.json](.vscode/c_cpp_properties.json) 里已经配了 `CUDA v13.1\include` 路径——这是给 IntelliSense(代码提示/高亮)用的,不影响实际编译。

---

## 4. 2.1.2 内核(Kernels)

### 4.1 定义内核:`__global__`(文档 2.1.2.1)

文档原文要点:
- **在 GPU 上执行、并且能从主机(CPU)调用的函数,叫 kernel**。
- 用 `__global__` 声明;内核必须返回 `void`。
- kernel 被设计成由**成千上万个线程同时执行同一份代码**。
- "kernel launch(内核启动)"就是从 CPU 发起、让 kernel 在 GPU 上跑的操作。

📌 **你的项目有 4 个 kernel**:

| kernel | 位置 | 职责 | 执行配置 |
|---|---|---|---|
| `render_init` | [main.cu:17](main.cu#L17) | 给每个像素初始化随机数种子 | `<<<blocks, threads>>>`(2D) |
| `render` | [main.cu:48](main.cu#L48) | 每个像素发射光线、算颜色 | `<<<blocks, threads>>>`(2D) |
| `create_world` | [main.cu:61](main.cu#L61) | 在显存里 `new` 出球和世界列表 | `<<<1, 1>>>` |
| `free_world` | [main.cu:72](main.cu#L72) | 在显存里 `delete` 掉场景 | `<<<1, 1>>>`(未被调用) |

四个函数全部 `__global__`、全部返回 `void`,完全符合文档规范。

### 4.2 启动内核:三重尖括号(文档 2.1.2.2 / 2.1.2.2.1)

文档原文要点:
- 并行执行的线程数量在**启动时**指定,叫 **execution configuration(执行配置)**。同一个 kernel 的每次调用可以用不同配置。
- 最常用的启动语法是**三重尖括号** `<<< >>>`:
  ```cpp
  vecAdd<<<1, 256>>>(A, B, C);
  ```
- 尖括号内**第一个参数是网格(grid)维度(有多少个块),第二个是线程块(block)维度(每块多少线程)**。
- 1D 时可以直接写整数;2D/3D 时用 `dim3` 类型。
- **每块最多 1024 个线程**——因为一个块的所有线程必须住在同一个流式多处理器(SM)上、共享 SM 的资源;资源允许时,一个 SM 上可以同时驻留多个块。
- **内核启动对主机线程是异步的**:CPU 发出启动命令后继续往下跑,不等 kernel 完成、甚至不等它开始。必须用某种同步手段才能确认 kernel 跑完了。

📌 **你的项目**:
- 2D 配置:
  ```cpp
  dim3 threads(8, 8);                                    // 每块 8×8 = 64 线程
  dim3 blocks((nx + threads.x - 1) / threads.x,          // 1000/8 = 125
              (ny + threads.y - 1) / threads.y);         // 500/8 → 63
  render<<<blocks, threads>>>(...);
  ```
  网格 125×63 个块,每块 64 线程。64 ≪ 1024,完全合法(选 8×8 是为了 2D 像素映射方便,教程惯例)。
- 1D 整数配置:
  ```cpp
  create_world<<<1, 1>>>(d_list, d_world);
  ```
  1 个块、1 个线程。注意:即使"只做一件事",也要以 kernel 形式启动——因为 `new` 必须发生在**设备代码**里,对象才能落在显存(详见 [第 15.5 节](#155-create_worldfree_world--设备端-newdelete))。
- **异步启动**是全文最重要的概念之一,你的两处 `cudaDeviceSynchronize` 就是为它存在的(见 [第 8 节](#8-214-同步-cpu-和-gpu))。

---

## 5. 2.1.2.3 线程与网格索引内建变量

文档原文要点:kernel 内部有 4 个内建变量(不用声明,直接可用):

| 内建变量 | 含义 |
|---|---|
| `threadIdx` | 当前线程**在其所属块内**的索引(每个线程互不相同) |
| `blockDim` | 块的大小(启动配置里指定的每块线程数) |
| `blockIdx` | 当前块**在网格内**的索引 |
| `gridDim` | 网格的大小(有多少个块) |

- 四个都是 **3 分量向量**,有 `.x` `.y` `.z` 成员;**启动时没指定的维度默认为 1**。
- `threadIdx` 和 `blockIdx` **从 0 开始编号**:`threadIdx.x` 取值 0 ~ blockDim.x-1;`blockIdx.x` 取值 0 ~ gridDim.x-1。
- 文档的 1D 公式:
  ```cpp
  int workIndex = threadIdx.x + blockDim.x * blockIdx.x;
  ```
  以 `vecAdd<<<4, 256>>>` 为例:第 0 块负责元素 0~255,第 1 块负责 256~511,第 2 块负责 512~767……`blockDim.x * blockIdx.x` 就是"前面所有块已经占掉的元素数"。

📌 **你的项目**:把这个公式推广到 2D——[main.cu:19-20](main.cu#L19-L20) 和 [main.cu:50-51](main.cu#L50-L51):
```cpp
int i = threadIdx.x + blockIdx.x * blockDim.x;  // 列方向(宽),对应像素的横坐标
int j = threadIdx.y + blockIdx.y * blockDim.y;  // 行方向(高),对应像素的纵坐标
```
- `i` 由 `.x` 分量拼出:`threadIdx.x` 是线程在 8×8 块内的列号(0~7),`blockIdx.x * 8` 是前面所有块的列数贡献,加起来就是全局列号 0~999。
- `j` 同理,用 `.y` 分量拼出全局行号 0~499。
- 于是 **(i, j) 与 (像素横坐标, 像素纵坐标) 一一对应**——这就是你前置知识注释里"给宽和高分别分线程"的落地方式。
- 文档说"未指定的维度默认为 1":你的 `<<<1,1>>>` 里 threadIdx 就是 (0,0,0),所以 [main.cu:63](main.cu#L63) 的 `threadIdx.x == 0` 成立。

---

## 6. 2.1.2.3.1 越界检查

文档原文要点:
- 当向量长度不是块大小的整数倍时,必须加边界判断,否则会**越界读写**:
  ```cpp
  int workIndex = threadIdx.x + blockDim.x * blockIdx.x;
  if (workIndex < vectorLength) { ... }   // 超出的线程直接不做任何事
  ```
- "块里有多余线程摸鱼"开销很小;但应避免启动**整块**都在摸鱼的块。
- 需要的块数 = 元素数 ÷ 每块线程数**向上取整**。整数技巧:`(vectorLength + threads - 1) / threads`;或用 CCCL 工具函数 `cuda::ceil_div(vectorLength, threads)`(头文件 `<cuda/cmath>`)。
- 每块 256 线程是文档随手选的,但通常是个不错的起点。

📌 **你的项目**——这是全项目最漂亮的对照点:
- 边界判断在 [main.cu:21-22](main.cu#L21-L22) 和 [main.cu:52-53](main.cu#L52-L53):
  ```cpp
  if ((i >= max_x) || (j >= max_y)) return;   // 越界线程提前退出
  ```
- 为什么你的项目**一定会触发**这个分支:`nx=1000` 能被 8 整除(blocks.x=125 正好),但 `ny=500` 不能被 8 整除:`500/8 = 62.5` → 向上取整得 63 块 → 共 `63×8 = 504` 行线程,比 500 行像素多了 4 行,即 `j = 500, 501, 502, 503` 的 `4×1000 = 4000` 个线程全部靠这个 `if` 提前 return。没有这个判断,`pixel_index = j * max_x + i` 会算出 500000 以后的索引,越界写 fb!
- 天花板除法:你用的是文档里的**整数技巧**(而不是 `cuda::ceil_div`),写法等价:
  ```cpp
  (nx + threads.x - 1) / threads.x   // (1000+7)/8 = 125
  (ny + threads.y - 1) / threads.y   // (500+7)/8 = 63
  ```

---

## 7. 2.1.3 内存:统一内存 vs 显式内存

文档原文要点:kernel 要用的数据必须放在 **GPU 能访问的内存**里。有两种方式:

### 7.1 统一内存(2.1.3.1)

- 用 `cudaMallocManaged` 分配(或 `__managed__` 变量),**NVIDIA 驱动自动管理数据在 CPU 和 GPU 之间的迁移**——谁访问,驱动就把数据搬到谁那边。
- 用 `cudaFree` 释放。
- 所有受支持的操作系统和 GPU 上都可用;某些 Linux 系统(有 ATS/HMM 的)上所有系统内存天然就是统一内存。

### 7.2 显式内存管理(2.1.3.2)

- `cudaMalloc` 在**GPU 上**分配显存,`cudaFree` 释放。
- `cudaMemcpy(dst, src, size, kind)` 在两边拷贝,最后参数是 `cudaMemcpyKind_t`:
  - `cudaMemcpyHostToDevice` 主机→设备;
  - `cudaMemcpyDeviceToHost` 设备→主机;
  - `cudaMemcpyDeviceToDevice` 设备内部/设备之间;
  - `cudaMemcpyDefault` 让 CUDA **根据指针自动推断方向**(文档示例用它)。
- `cudaMemcpy` 是**同步**的:拷贝完才返回。异步拷贝在后面的"CUDA 流"章节。
- `cudaMallocHost` 分配**页锁定(pinned)主机内存**:拷贝性能更好、且异步传输必需;但锁太多页会拖慢系统,只该锁真正要传输的缓冲区。`cudaMemset` 用来清零设备内存。

### 7.3 权衡(2.1.3.3)

- 显式管理更啰嗦,但**控制权在你手上**:什么时候拷、数据住在哪、能不能把传输和计算重叠。
- 统一内存也有 `cudaMemPrefetchAsync`/`cudaMemAdvise` 等提示 API(文档预告,后面章节讲),能拿回一部分显式管理的性能优势。

📌 **你的项目——一个教科书式的混合策略**:
- **`fb` 用统一内存**([main.cu:87](main.cu#L87)):
  ```cpp
  cudaMallocManaged(&fb, fb_size);   // fb_size = 1000*500*sizeof(vec3) = 500000*12 字节 ≈ 5.7 MB
  ```
  因为 fb 是 **GPU 写、CPU 读**(main 末尾打印 PPM),统一内存让你省掉一次显式的 `cudaMemcpy(DeviceToHost)`。GPU 写完后 CPU 直接读,驱动自动把数据迁回。
- **`d_rand_state`、`d_list`、`d_world` 用显式设备内存**([main.cu:91,96-97](main.cu#L91)):
  ```cpp
  cudaMalloc((void**)&d_rand_state, num_pixels * sizeof(curandState)); // 50万×48字节 ≈ 24 MB
  cudaMalloc((void**)&d_list, 2 * sizeof(hitable*));                  // 存 2 个指针
  cudaMalloc((void**)&d_world, sizeof(hitable*));                     // 存 1 个指针
  ```
  因为这三样**从头到尾只有 GPU 碰**,用普通显存最高效,不需要迁移能力。
- 你没用到 `cudaMemcpy`/`cudaMallocHost`/`cudaMemset`——文档示例里 `cudaMemset(devC, 0, ...)` 清零输出数组是因为 vecAdd 可能留空洞;你的 fb **每个像素都会被写入**,所以不需要清零。
- 注意一个微妙点:`d_list` 和 `d_world` 本身是**主机变量**,但它们保存的是**设备内存地址**(`cudaMalloc` 返回的指针指向显存)。主机代码拿到这种指针只能当"地址存根"用,不能解引用。

---

## 8. 2.1.4 同步 CPU 和 GPU

文档原文要点:
- kernel 启动是异步的,要确认它跑完,**最简单的同步手段是 `cudaDeviceSynchronize()`**:阻塞主机线程,直到 GPU 上**所有先前提交的工作**(所有流)全部完成。
- 大应用里可能有多条流(stream),用流级同步或 CUDA Event 更精细;文档指向"异步执行"章节。

📌 **你的项目有两处调用**,但作用不同,这是考点:
1. [main.cu:103](main.cu#L103)(render_init 之后),注释写"必须等世界造完才能渲染"。**严格说这个同步不是必须的**:你的所有 kernel 都启动在**默认流(default stream)**上,默认流保证"先提交先执行、顺序完成",所以 `create_world` 一定先于 `render_init` 先于 `render` 执行完毕,**GPU 内部的先后顺序不需要 CPU 同步来保证**。`cudaDeviceSynchronize` 管的是"CPU 等 GPU",不是"GPU 之间排队"。这个同步无害但可省略。
2. [main.cu:111](main.cu#L111)(render 之后)——**这才是必须的**:如果没有它,CPU 可能在 render 还没算完时就冲进打印循环读 fb,拿到全 0 或未初始化垃圾。这就是文档"kernel 启动异步于主机线程"的直接后果。

一句话总结:**同步点守护的是"CPU 读结果"这个动作,不是"GPU 上的顺序"。**

---

## 9. 2.1.5 综合示例 + __syncthreads

文档原文要点:
- 给出 vecAdd 两个完整版本(统一内存版/显式内存版)的完整代码,流程模板是:**分配 → 初始化 → 启动 kernel → 同步 → 串行验证结果 → 清理**。
- 验证手法:CPU 上串行算一遍(serialVecAdd),与 GPU 结果逐元素比较(vectorApproximatelyEqual,容差 1e-5)。这也解释了两版输出 "CPU and GPU answers match"。
- 末尾一段讲线程协作:
  - `__syncthreads()`:块内所有线程的**屏障(barrier)**——块内所有线程都到达后才能继续;只同步**本块**,不跨块。
  - 共享内存(shared memory)是 SM 上靠近计算核心的低延迟内存(类似 L1),配合 `__syncthreads` 用于块内协作。
  - **块间同步**只在特定机制下支持(线程块簇、Cooperative Groups)。
  - 跨块协作通常用**原子内存函数(atomic)**。
  - 最佳性能来自把同步限制在块内。

📌 **你的项目**:
- main() 的流程与文档模板一一对应:分配(fb + 三块显存)→ 初始化(create_world 造场景、render_init 造随机种子)→ 启动(render)→ 同步(行 111)→ 验证(打印 PPM,肉眼验证图像)→ 清理(cudaFree(fb))。
- 你的项目**全程没有 `__syncthreads`、没有共享内存、没有原子操作**——这是完全正确的:每个像素独立,线程之间零通信、零依赖,正是文档说的"所有线程做独立工作"的最理想情况(图形学里叫 embarrassingly parallel)。什么时候才需要?做**图像模糊/卷积**(要读邻居像素)时,把一块图像载入 `__shared__`,用 `__syncthreads` 保证全载完再算;多个线程累加同一个统计量时才需要 atomic。你的光线追踪暂时都用不上。

---

## 10. 2.1.6 运行时初始化

文档原文要点:
- CUDA Runtime 为每个设备创建一个 **CUDA context(上下文)**,这是该设备的 **primary context**,在**第一次需要它的运行时调用**时惰性初始化;设备代码在此时被 JIT 编译并加载进显存。这一切**透明发生**。
- primary context 被应用的所有主机线程共享;可从 Driver API 访问(互操作)。
- CUDA 12.0 起,`cudaSetDevice`/`cudaInitDevice` 会显式初始化运行时及指定设备的 primary context;之前版本则延迟到第一次调用。未显式指定时,运行时**隐式使用设备 0** 并自初始化。因此要检查 `cudaSetDevice` 的返回值。
- `cudaDeviceReset()` 销毁当前设备的 primary context,之后再调 API 会重建。
- ⚠️ CUDA 接口使用**在 main 之前初始化、main 结束后销毁**的全局状态;在程序初始化期或 main 之后的析构期使用这些接口是**未定义行为**(例如:在全局对象的析构函数里调 cudaFree 是 UB)。

📌 **你的项目**:你从没写过任何初始化代码——[main.cu:87](main.cu#L87) 的 `cudaMallocManaged(&fb, ...)` 是 main() 里第一个 CUDA 调用,它隐式完成了:初始化运行时 → 在设备 0 上创建 primary context → 加载/JIT 你的 kernel。你没调用 `cudaSetDevice`,所以默认用 0 号卡(单卡场景无所谓)。

---

## 11. 2.1.7 错误检查

文档原文要点:
- 每个 CUDA API 返回 `cudaError_t` 枚举;无错时是 `cudaSuccess`。生产代码应检查每个返回值,常用 `CUDA_CHECK` 宏封装(用 `cudaGetErrorString` 把错误码翻译成人类可读字符串)。
- **错误状态(2.1.7.1)**:运行时为**每个主机线程**维护一个错误状态,默认 `cudaSuccess`,出错时被覆盖。`cudaGetLastError()` 读取并**清零**;`cudaPeekAtLastError()` 只读不清。
- **三重尖括号启动不返回错误码**。启动后立即查错误,只能确认**启动参数合法**;"立即查到 cudaSuccess"不代表 kernel 已成功执行、甚至不代表它已开始执行。
- **异步错误(2.1.7.2)**:kernel 执行期间发生的错误(如非法内存访问)是异步报上来的,会在下一次检查错误状态时被看到;**错误状态有粘性**——不清除的话,之后每个返回 cudaError_t 的 API 都会返回同一个错。典型写法:
  ```cpp
  vecAdd<<<...>>>(...);
  CUDA_CHECK(cudaGetLastError());        // 查启动错误
  CUDA_CHECK(cudaDeviceSynchronize());   // 查执行期错误
  ```
  (`cudaStreamQuery`/`cudaEventQuery` 返回的 `cudaErrorNotReady` 不算错误。)
- **CUDA_LOG_FILE(2.1.7.3,驱动 r570+)**:设置该环境变量为文件路径,驱动会把错误详情写进该文件(比错误码详细,例如会告诉你块尺寸 (4096,1,1) 超过了最大值 (1024,1024,64));设为 `stdout`/`stderr` 则直接打印。即使程序没做错误检查也能捕获错误,还能注册回调函数把日志接进应用自己的日志系统。

📌 **你的项目——这是你项目缺的一课**:全程没有任何错误检查。对学习项目来说能跑就行,但建议立即补上(见 [附录](#17-附录编译命令改进建议)):
- 每个 `cudaMalloc*` 包上检查;
- 每个 kernel 启动后加 `cudaGetLastError()`;
- 每个 `cudaDeviceSynchronize()` 的返回值要查(这是唯一能捕获 kernel 执行期错误的地方)。
- Windows 上设环境变量的命令:`$env:CUDA_LOG_FILE="cuda_log.txt"`(PowerShell),再运行程序即可拿到驱动级错误详情。

---

## 12. 2.1.8 设备与主机函数

文档原文要点:
- `__global__`:kernel 入口,通常在主机启动;用**动态并行(dynamic parallelism)**也可以在设备端启动 kernel。
- `__device__`:编译成 GPU 代码,只能被其他 `__device__` 或 `__global__` 函数调用。
- **`__host__ __device__` 双修饰**:同时生成 CPU 和 GPU 两份代码;适用于函数,**包括类成员函数、仿函数(functor)、lambda**。

📌 **你的项目——三种修饰符全用上了**:
| 修饰符 | 你的例子 | 为什么 |
|---|---|---|
| `__global__` | `render`、`render_init`、`create_world`、`free_world` | kernel 入口,主机启动 |
| `__device__` | [main.cu:30](main.cu#L30) 的 `color()`、[hitable_list.h:25](hitable_list.h#L25) 的 `hitable_list::hit`、[sphere.h](sphere.h) 的构造/hit | 只在 GPU 上被调用,不生成 CPU 版本 |
| `__host__ __device__` | [vec3.h](vec3.h) 和 [ray.h](ray.h) 里几乎所有成员函数 | **主机和设备都要用同一份源码**:main()(CPU)构造 `vec3 lower_left_corner(-2.0, -1.0, -1.0)`、调用构造函数;render kernel(GPU)里也调用同一构造函数和运算符。nvcc 编两遍:主机遍交给 cl.exe,设备遍交给 cicc |

一个容易踩的坑:`color` 为什么只能标 `__device__` 不能标 `__host__`?因为它的参数里有 `hitable **world` 这种指向**设备内存**的指针,CPU 版本根本无法执行。而 vec3/ray 的成员不碰任何设备专属资源,所以可以双修饰。

---

## 13. 2.1.9 变量说明符

文档原文要点,静态变量可加说明符控制存放位置:
| 说明符 | 存放位置 |
|---|---|
| `__device__` | 全局内存(Global Memory) |
| `__constant__` | 常量内存(Constant Memory) |
| `__managed__` | 统一内存(Unified Memory) |
| `__shared__` | 共享内存(Shared Memory) |

- 在 `__device__`/`__global__` 函数内**无说明符**的局部变量:编译器尽量放**寄存器**,放不下时落到**局部内存(local memory,物理上在全局内存)**。
- 在函数外**无说明符**的变量:在**系统内存**(主机侧)。

### 2.1.9.1 检测设备编译:`__CUDA_ARCH__`

- 对 `__host__ __device__` 函数,nvcc 要编两份;想在两份里写不同代码,用预处理器检查 `__CUDA_ARCH__`——它**只在设备编译遍(device pass)有定义**,值是架构版本号(如 750、900、1200)。

📌 **你的项目映射**:
- [sphere.h:18-21](sphere.h#L18-L21) 的 `oc`、`a`、`b`、`c`、`discriminant`、`sq`、`T_1`、`T_2` 都是 kernel 内局部变量 → 编译器会尽量把它们放**寄存器**(最理想的归宿)。
- `hit_record rec` 通过**引用**传递,占据调用线程的寄存器/局部内存;`temp_rec` 同理。
- 两个 sphere 对象的 `center`、`radius` 成员 → 对象本身是设备端 `new` 出来的,住在**全局内存(设备堆)**。
- [main.cu:83-84](main.cu#L83-L84) 的 `nx`、`ny`、`fb_size` 在函数外/普通主机函数内 → **系统内存**。
- 你**没用** `__shared__`、`__constant__`、`__managed__` 变量说明符(fb 是通过 `cudaMallocManaged` **API** 分配的托管内存,不是 `__managed__` **变量**——两条路都通向统一内存,文档 2.1.3.1 说得很清楚)。
- 你也没用 `__CUDA_ARCH__`(没有需要区分两份代码的场景)。

---

## 14. 2.1.10 线程块簇

文档原文要点:
- 计算能力(compute capability)≥ 9.0(Hopper H100 及更新架构)起,编程模型多了一层可选层级:**线程块簇(thread block cluster)**,由若干线程块组成。
- 类比"块内线程保证同驻一个 SM",**簇内线程块保证同驻一个 GPU Processing Cluster(GPC)**,从而支持**硬件级跨块同步**(`cluster.sync()`)、分布式共享内存(簇内所有块的共享内存拼成一片大共享内存,可互相读写和原子操作)。
- 可移植的簇大小上限是 **8 个块**(小 GPU 或 MIG 上会更小,可用 `cudaOccupancyMaxPotentialClusterSize` 查询);网格仍是簇的 1D/2D/3D 排列。
- 启动方式:编译期 kernel 属性 `__cluster_dims__(X, Y, Z)` + 普通 `<<<>>>`(簇大小编译期固定,grid 维度必须能被簇大小整除;`gridDim` 仍按**块数**计数),或 `cudaLaunchKernelEx` 运行时指定。
- Cooperative Groups 的 `cluster` 组提供 `num_threads()`/`num_blocks()`/`dim_threads()`/`dim_blocks()` 等查询。

📌 **你的项目**:完全没有用到(绝大多数入门代码都不会用)。你只需要记住:这是 CC 9.0+ 的高阶可选层级,等你要做**跨块同步/块间共享数据**的算法(如分布式直方图)时再回头学。你的代码所有同步需求都停留在"块内、甚至线程内",用不上。

---

## 15. 项目逐文件深度复习

> 这一部分是文档 2.1 **没讲**、但你的项目里存在的知识点。它们一部分来自 CUDA 编程指南后面章节(curand、设备运行时堆),一部分是图形学/C++ 内容。

### 15.1 main.cu 总流程

| 步骤 | 行号 | 动作 | 知识点 |
|---|---|---|---|
| 1 | [main.cu:83-87](main.cu#L83-L87) | 定尺寸、分配 fb(统一内存) | 2.1.3.1 |
| 2 | [main.cu:90-91](main.cu#L90-L91) | 分配 d_rand_state(设备内存) | 2.1.3.2 |
| 3 | [main.cu:94-98](main.cu#L94-L98) | 分配 d_list/d_world,启动 create_world | 设备端 new |
| 4 | [main.cu:100-102](main.cu#L100-L102) | dim3 配置,启动 render_init | 2.1.2.2 / 越界 |
| 5 | [main.cu:103](main.cu#L103) | cudaDeviceSynchronize | 2.1.4(可省略,见第 8 节) |
| 6 | [main.cu:104-110](main.cu#L104-L110) | 相机参数、启动 render | 相机几何 |
| 7 | [main.cu:111](main.cu#L111) | cudaDeviceSynchronize | 2.1.4(**必须**) |
| 8 | [main.cu:112-125](main.cu#L112-L125) | 打印 PPM | PPM 格式 |
| 9 | [main.cu:126](main.cu#L126) | cudaFree(fb) | 2.1.3.1 |

### 15.2 render_init 与 curand 随机数

[curand_kernel.h](main.cu#L7) 是 CUDA 随机数库的**设备端**头文件(对应编程指南的随机数章节,2.1 未讲)。

- `curandState` 是一个随机数生成器**状态机**(本结构 48 字节,存 XorWow 算法的内部状态)。
- `curand_init(seed, subsequence, offset, &state)` 初始化它:
  - **seed**:序列主种子;
  - **subsequence / offset**:在同一主序列内继续切分的子序列号/偏移(你填 0);
- [main.cu:26](main.cu#L26) 的精华一行:
  ```cpp
  curand_init(1984 + pixel_index, 0, 0, &rand_state[pixel_index]);
  ```
  用固定基数 1984 **加上像素索引**当种子 → 50 万个像素各自拥有**互不相同、互不相关**的独立随机序列。这正是注释说的"确保这几万个骰子摇出来的轨迹完全不同"。
- ⚠️ **重要提醒:目前 `d_rand_state` 还没有被任何代码消费**!`render` 里还没有随机采样。这不是漏写——它是教程为下一步**抗锯齿(anti-aliasing)**准备的:每个像素随机偏移采样多次取平均。你现在会看到球边缘有锯齿,下一步就靠它消除。

### 15.3 color():着色核心([main.cu:30-45](main.cu#L30-L45))

```cpp
__device__ vec3 color(const ray &r, hitable **world)
{
    hit_record rec;
    if ((*world)->hit(r, 0.001f, 10000.0f, rec))   // 命中?
        return 0.5f * vec3(rec.normal.x()+1, rec.normal.y()+1, rec.normal.z()+1);
    // 未命中 → 天空渐变
    vec3 unit_direction = unit_vector(r.direction());
    float t = 0.5f * (unit_direction.y() + 1.0f);
    return (1.0f - t) * vec3(1.0,1.0,1.0) + t * vec3(0.5,0.7,1.0);
}
```
知识点:
- `t_min = 0.001f` 而不是 0:防止**浮点自交**——误差会让"从表面出发"的光线立刻再次命中同一表面(t≈0),把 0 附近的一小段剔除(即阴影痤疮 shadow acne 的预防)。
- `t_max = 10000.0f`:能"看"到的最大距离,超过视为没打中。
- **法线着色**:法线分量 ∈ [-1,1],`0.5*(n+1)` 映射到 [0,1] 当 RGB——这就是当前每张图里球体有渐变感的来源。
- **天空**:光线方向归一化后取 y 分量 → 映射到 [0,1] → 在白色 `(1,1,1)` 与浅蓝 `(0.5,0.7,1.0)` 之间做**线性插值(lerp)**:`(1-t)*A + t*B`。t=0 纯白(仰视),t=1 纯浅蓝(平视)。
- 这就是你前置知识 3 的实现:颜色就是一个 vec3,R/G/B 对应 x/y/z。

### 15.4 render():相机与视口几何([main.cu:48-60](main.cu#L48-L60))

```cpp
float u = float(i) / float(max_x);   // 列位置归一化到 [0,1)
float v = float(j) / float(max_y);   // 行位置归一化到 [0,1)
ray r(origin, lower_left_corner + u * horizontal + v * vertical);
```
- 相机参数(经典 Peter Shirley 设定):`origin(0,0,0)`,`lower_left_corner(-2,-1,-1)`,`horizontal(4,0,0)`,`vertical(0,2,0)`。视口平面在 z = -1,宽 4、高 2,长宽比 2:1,与图像 1000:500 匹配。
- 光线方向 = `lower_left_corner + u*horizontal + v*vertical`(origin 是原点,所以方向向量恰好等于视口上的点坐标)。
- 注意 **v 没有翻转**:j=0 是图像顶部,对应世界坐标 y≈+1。翻转延迟到打印 PPM 时完成(见 15.12)。
- `fb[pixel_index] = color(r, world)` 直接把颜色写进统一内存——GPU 写、CPU 后读。

### 15.5 create_world / free_world:设备端 new/delete

```cpp
__global__ void create_world(hitable **d_list, hitable **d_world)
{
    if (threadIdx.x == 0 && blockIdx.x == 0)
    {
        d_list[0] = new sphere(vec3(0,0,-1.0f), 0.5f);       // 悬空的球
        d_list[1] = new sphere(vec3(0,-100.5f,-1.0f), 100.0f); // 巨大的"地面"
        *d_world = new hitable_list(d_list, 2);
    }
}
```
知识点:
- **设备端 `new`/`delete`**(文档 2.1 没讲):在 kernel 里 `new` 出来的对象分配在**设备运行时堆(device runtime heap)**,存于显存。为什么必须这样造场景?因为 `sphere`/`hitable_list` 含**虚函数**,对象(含虚表指针)必须完整地存在于显存,最干净的做法就是让 GPU 自己构造。释放用配套的 `free_world` 里 `delete`(设备端 `delete`),不能用主机 `delete`。
- `<<<1,1>>>` + `if (threadIdx.x == 0 && blockIdx.x == 0)`:保证只执行一次(配置本身就是 1 线程,条件是防御性双保险)。
- 场景内容:球 1 圆心 (0,0,-1) 半径 0.5(悬空);球 2 圆心 (0,-100.5,-1) 半径 100(顶部 y=-0.5,恰好在球 1 下方当"地面")。
- ⚠️ `free_world` **定义了但从未被调用**;`d_rand_state`/`d_list`/`d_world` 也没有 `cudaFree`。进程退出时操作系统和 CUDA 驱动会回收,不算致命,但正规代码应释放(见附录)。

### 15.6 指针的指针 hitable**(全项目最绕的 C++ 点)

- `render` 接收 `hitable **world`,而不是 `hitable *`。为什么?
  - 世界列表对象的地址是 **GPU 在 create_world 里 new 出来的**,主机永远不知道;
  - 所以主机提前 `cudaMalloc` 一块 8 字节的**设备内存** `d_world`,create_world 把地址写进去(`*d_world = new hitable_list(...)`),render 再把它读出来。
  - **两个 kernel 通过这块小内存交接对象的地址**——这正是 C 语言"想让函数修改指针本身,就传指针的指针"的设备内存版。
- 解引用链:`world`(设备内存里的指针槽)→ `*world`(hitable_list 对象的指针)→ `(*world)->hit(...)`(虚调用)。你的 [main.cu:33](main.cu#L33) 用的正是 `(*world)->hit(...)`。
- 注意主机侧不能对 `d_world` 解引用——它指向显存,CPU 直接读会崩。

### 15.7 hitable.h:设备端虚函数与命中记录

- `hit_record`([hitable.h:11-16](hitable.h#L11-L16)):光线"行车记录仪",存三个量:`t`(从光源沿光线到命中点的距离参数)、`p`(命中点坐标)、`normal`(命中点单位法线)。
- [hitable.h:24](hitable.h#L24):
  ```cpp
  __device__ virtual bool hit(const ray &r, float t_min, float t_max, hit_record &rec) const = 0;
  ```
  - **GPU 上支持虚函数**(需要对象在设备内存,你的满足;虚表由 nvcc 生成在设备端);
  - 纯虚函数 = 抽象基类:凡"能被光线击中"的物体(球、列表)都必须实现自己的 `hit`。
  - `rec` 用**引用**传出命中信息(对应文档 2.1.2 里 kernel 参数按值传递的延伸:设备端函数参数与 C++ 一样支持引用)。

### 15.8 hitable_list.h:最近命中算法

- `hitable_list` 持有 `hitable **list`(物体指针数组)+ `list_size`。
- [hitable_list.h:25-40](hitable_list.h#L25-L40) 的 `hit` 核心逻辑:
  ```cpp
  float closet_so_far = t_max;      // 当前最近的命中距离,初值=最大可视距离
  for (int i = 0; i < list_size; i++)
      if (list[i]->hit(r, t_min, closet_so_far, temp_rec)) {
          hit_anything = true;
          closet_so_far = temp_rec.t;   // 收缩上界
          rec = temp_rec;               // 值拷贝
      }
  ```
  三个知识点:① **最近者胜**——遍历全部物体,每次命中都把上界收缩到该距离,最后 `rec` 里留下最近的;② 把 `closet_so_far` 作为下一个物体的 `t_max` 传入,比它远的命中直接被拒(小优化);③ `rec = temp_rec` 是**值拷贝**(结构体里含 vec3,拷贝整个命中记录)。

### 15.9 sphere.h:光线-球求交数学

[sphere.h:16-47](sphere.h#L16-L47) 是 Peter Shirley《Ray Tracing in One Weekend》第 5 章的经典求交,推导如下:

光线 `P(t) = A + t·B`,球心 C、半径 R,求 `|P(t) - C|² = R²` 的解。令 `oc = A - C`:
```
|oc + t·B|² = R²
t²(B·B) + 2t(oc·B) + (oc·oc - R²) = 0        →  a t² + b t + c = 0
a = dot(B, B)
b = 2·dot(oc, B)
c = dot(oc, oc) - R²
```
- **判别式** `discriminant = b² - 4ac`:<0 无实根,光线错过球(return false);=0 相切(代码取 `> 0`,严格相切会漏,误差可忽略)。
- `sqrt` 只算一次存进 `sq`(性能细节)。
- 两个根 `T_1 = (-b-sq)/(2a)`(近交点)、`T_2 = (-b+sq)/(2a)`(远交点),**按近→远顺序**检验是否落在 `[t_min, t_max]` 内——先看近的,近的合法就选它(挡住后面的)。
- 命中记录三件套:`rec.t = T_1`;`rec.p = r.point_at_parameter(rec.t)`(用 ray.h 的公式算命中点);`rec.normal = (rec.p - center) / radius`(从球心指向命中点再除以半径 → 单位法线)。
- 因为 [hitable_list.h:32](hitable_list.h#L32) 传进来的 t_max 是"当前最近距离",远处被挡住的根自然会被拒掉,最终得到的就是整张图里最近的命中。

### 15.10 ray.h:光线的数学表示

- 光线 = 起点 + 参数化方向:**`P(t) = A + t·B`**([ray.h:22](ray.h#L22) 的 `point_at_parameter`)。t 是标量"距离参数",t=0 是起点,t>0 沿方向前进——这正是 `hit_record.t` 里那个 t 的来历。
- 两个构造函数和所有 getter 都标 `__host__ __device__`:主机侧构造相机参数对象、设备侧构造光线,两份代码都要。

### 15.11 vec3.h:一个类型,两种身份

- 数据:`float e[3]`([vec3.h:43](vec3.h#L43)),12 字节无填充。
- **几何向量与颜色的复用**:`x()/y()/z()` 与 `r()/g()/b()` 是同一存储的两个别名([vec3.h:20-25](vec3.h#L20-L25))——这就是"颜色就是 vec3"的实现。
- 运算符全景(注意 `*` 是**逐分量乘**,不是点积!):
  - 一元:`+`、`-`、下标 `operator[]`(const 与非 const 两个版本);
  - 复合赋值:`+= -= *= /=`(vec3 版)和 `*= /=`(float 版),定义在类外 [vec3.h:113-161](vec3.h#L113-L161);
  - 自由函数:`+ - * /`(vec3×vec3 逐分量)、标量 `t*v` 和 `v*t`、`v/t`([vec3.h:66-99](vec3.h#L66-L99));
  - `dot`(点积)、`cross`(叉积,[vec3.h:101-111](vec3.h#L101-L111))、`length`、`squared_length`、`make_unit_vector`(原地归一化)、`unit_vector`(返回归一化副本)。
- 流操作符 `>>` `<<` **只能标 `__host__`**([vec3.h:46-56](vec3.h#L46-L56)):因为 `std::istream/ostream` 在设备端不存在,没有设备版本可生成。
- 头文件保护 `#ifndef VEC3H ... #endif`:防止同一头文件被多次包含导致重定义——所有 .h 文件都有这套。

### 15.12 PPM 输出与 y 轴翻转([main.cu:112-125](main.cu#L112-L125))

- PPM P3 格式:魔数 `P3` → `宽 高` → 最大色值 `255` → 之后每个像素三个十进制数。
- 打印循环 `for (int j = ny-1; j >= 0; j--)` **从最后一行倒序打印**:图像文件的行是从上往下存的(第 0 行是图片顶部),而世界坐标 y 向上——渲染时 j=0 对应世界 y≈+1(顶部),所以要把行序倒过来,图片才不上下颠倒。这就是 15.4 里"v 不翻转、打印时翻"的呼应。
- `int(255.99 * value)`:把 [0,1] 浮点色映射到 0~255 整数,`255.99` 让 1.0 截断成 255 而不是溢出成 256。
- 读 fb 是在 `cudaDeviceSynchronize()` 之后,统一内存保证此时读到的就是 GPU 算完的数据。

### 15.13 已知小问题清单(你的项目)

1. 无任何错误检查(2.1.7);
2. `free_world` 定义未调用;`d_rand_state`、`d_list`、`d_world` 未 `cudaFree`(进程退出时驱动兜底,但正规应释放);
3. `d_rand_state` 已初始化但尚未被使用(为下一步抗锯齿预留,不是 bug);
4. [main.cu:103](main.cu#L103) 的注释"必须等世界造完才能渲染"理由不精确——GPU 内顺序由默认流保证,该同步可省略(见第 8 节);
5. tasks.json 里是 g++ 任务,编不了 .cu(见第 3 节)。

---

## 16. 两张总对照表

### 表 A:文档 2.1 知识点 → 你项目的落点(零遗漏核对)

| # | 文档知识点 | 文档小节 | 你项目中的体现 |
|---|---|---|---|
| 1 | Runtime API 与 Driver API 的关系 | 2.1 导言 | 只用 Runtime API(cudaMallocManaged/cudaMalloc/cudaDeviceSynchronize/<<<>>>) |
| 2 | nvcc 编译 / .cu | 2.1.1 | main.cu 整体一个编译单元;tasks.json 的 g++ 任务不能用(见附录) |
| 3 | `__global__` 定义 kernel、void 返回 | 2.1.2.1 | render_init / render / create_world / free_world |
| 4 | 执行配置(每次启动可不同) | 2.1.2.2 | render 用 2D 配置,create_world 用 <<<1,1>>> |
| 5 | 三重尖括号语法、grid 与 block 参数顺序 | 2.1.2.2.1 | `render<<<blocks, threads>>>(...)` |
| 6 | 1D 整数配置 / dim3 2D 配置 | 2.1.2.2.1 | `<<<1,1>>>` / `dim3 threads(8,8)`、`dim3 blocks(125,63)` |
| 7 | 每块 ≤1024 线程、SM 共享资源 | 2.1.2.2.1 | 64 线程/块,合法且有余量 |
| 8 | 内核启动对主机异步 | 2.1.2.2.1 | 两处 cudaDeviceSynchronize(见 #22) |
| 9 | threadIdx / blockIdx / blockDim / gridDim | 2.1.2.3 | `i = threadIdx.x + blockIdx.x * blockDim.x` 及 `.y` 版本 |
| 10 | 3 分量向量、未指定维度=1、零索引 | 2.1.2.3 | <<<1,1>>> 中 threadIdx=(0,0,0);.z 未使用默认为 1 |
| 11 | workIndex 公式及其 2D 推广 | 2.1.2.3 | i(列)、j(行)两条公式 |
| 12 | 越界检查 | 2.1.2.3.1 | `if (i >= max_x \|\| j >= max_y) return;` |
| 13 | 天花板除法 / cuda::ceil_div | 2.1.2.3.1 | `(nx + threads.x - 1) / threads.x` 整数技巧 |
| 14 | 统一内存 cudaMallocManaged/cudaFree | 2.1.3.1 | fb(需回传 CPU 打印) |
| 15 | 显式内存 cudaMalloc/cudaMemcpy 四类/cudaMallocHost/cudaMemset | 2.1.3.2 | d_rand_state、d_list、d_world 用 cudaMalloc;未用 memcpy(统一内存代替)与 pinned 内存 |
| 16 | 统一 vs 显式 的性能权衡 | 2.1.3.3 | 混合策略:回传数据用 Managed,纯 GPU 数据用显存 |
| 17 | cudaDeviceSynchronize | 2.1.4 | [main.cu:103](main.cu#L103)(可省略)、[main.cu:111](main.cu#L111)(必须) |
| 18 | 完整程序流程模板(分配→启动→同步→验证→清理) | 2.1.5 | main() 九步流程(见 15.1) |
| 19 | __syncthreads / 共享内存 / 原子操作 / 块间同步 | 2.1.5 末段 | 全部未使用——像素相互独立,不需要协作 |
| 20 | 运行时惰性初始化、primary context、隐式设备 0 | 2.1.6 | 首个 cudaMallocManaged 隐式初始化;未调 cudaSetDevice/cudaDeviceReset |
| 21 | 错误检查:cudaError_t/CUDA_CHECK/cudaGetLastError/异步粘性错误/CUDA_LOG_FILE | 2.1.7 | 项目未做(建议补,见附录) |
| 22 | `__device__` / `__host__ __device__` / 动态并行 | 2.1.8 | color 等用 __device__;vec3.h、ray.h 双修饰;未用动态并行 |
| 23 | 变量说明符 __shared__/__constant__/__managed__/寄存器/局部内存 | 2.1.9 | kernel 局部变量在寄存器;对象成员在设备堆(全局内存);未用三个说明符 |
| 24 | __CUDA_ARCH__ | 2.1.9.1 | 未使用 |
| 25 | 线程块簇(CC 9.0+、cluster.sync、分布式共享内存) | 2.1.10 | 未使用(入门无需) |

### 表 B:你项目的知识点 → 出处(零遗漏核对)

| # | 项目知识点 | 位置 | 出处 |
|---|---|---|---|
| 1 | 像素↔线程 2D 映射(i、j) | render / render_init | 文档 2.1.2.3 |
| 2 | 越界早退 | render / render_init | 文档 2.1.2.3.1 |
| 3 | 统一内存 fb | main.cu:87 | 文档 2.1.3.1 |
| 4 | 设备内存 d_rand_state/d_list/d_world | main.cu:91,96-97 | 文档 2.1.3.2 |
| 5 | 异步启动 → 同步点 | main.cu:103,111 | 文档 2.1.4 |
| 6 | 设备端 new/delete(device heap) | create_world / free_world | CUDA 编程指南设备内存章节(2.1 未讲,本文 15.5) |
| 7 | `__device__` 虚函数多态 | hitable.h | 文档 2.1.8(虚函数为项目延伸) |
| 8 | 指针的指针 hitable** 交接对象 | d_world 全链路 | C/C++ 语言基础(本文 15.6) |
| 9 | curand_init 每像素独立种子 | render_init | CUDA 随机数章节(2.1 未讲,本文 15.2) |
| 10 | 光线-球求交(判别式、t1/t2) | sphere.h | 图形学数学(本文 15.9) |
| 11 | 最近命中 + t_max 收缩 | hitable_list.h | 图形学(本文 15.8) |
| 12 | 相机/视口几何(u、v、lower_left_corner) | render | 图形学(本文 15.4) |
| 13 | 法线着色 0.5*(n+1)、天空 lerp | color | 图形学(本文 15.3) |
| 14 | t_min=0.001 防自交 | color | 图形学(本文 15.3) |
| 15 | PPM P3 输出、255.99、行序翻转 | main 打印循环 | 图像格式(本文 15.12) |
| 16 | vec3 颜色/坐标复用、运算符重载 | vec3.h | C++/图形学(本文 15.11) |
| 17 | ray 参数方程 P(t)=A+tB | ray.h | 图形学(本文 15.10) |
| 18 | 头文件保护、多文件组织 | 全部 .h | C++ 基础 |
| 19 | nvcc vs g++ 构建配置 | .vscode/*.json | 文档 2.1.1(本文第 3 节) |

---

## 17. 附录:编译命令、改进建议

### 17.1 正确的编译命令(Windows)

```powershell
# 在项目目录(已安装 CUDA Toolkit + MSVC 环境)下:
nvcc main.cu -o main.exe
.\main.exe > out.ppm
```
- 可加 `-arch=native` 让 nvcc 针对你本机 GPU 架构生成代码(或显式指定如 `-arch=sm_75`)。
- 建议把 [.vscode/tasks.json](.vscode/tasks.json) 里的 g++ 任务换成 nvcc 任务,或直接用终端编译。

### 17.2 建议补上的错误检查(对应 2.1.7)

```cpp
#define CUDA_CHECK(expr) do {                                \
    cudaError_t _err = (expr);                               \
    if (_err != cudaSuccess) {                               \
        fprintf(stderr, "CUDA Error: %s:%d -> %s\n",         \
                __FILE__, __LINE__, cudaGetErrorString(_err));\
        exit(1);                                             \
    }                                                        \
} while(0)
```
用法:
```cpp
CUDA_CHECK(cudaMallocManaged(&fb, fb_size));
CUDA_CHECK(cudaMalloc((void**)&d_rand_state, ...));
create_world<<<1,1>>>(d_list, d_world);
CUDA_CHECK(cudaGetLastError());            // 启动错误
render<<<blocks, threads>>>(...);
CUDA_CHECK(cudaGetLastError());
CUDA_CHECK(cudaDeviceSynchronize());       // 执行期错误
```

### 17.3 资源释放补全(对应 2.1.3)

在 main() 末尾补上:
```cpp
free_world<<<1,1>>>(d_list, d_world);
CUDA_CHECK(cudaGetLastError());
CUDA_CHECK(cudaDeviceSynchronize());
cudaFree(d_world);
cudaFree(d_list);
cudaFree(d_rand_state);
cudaFree(fb);
```

### 17.4 下一步(教程后续章节预告)

- **抗锯齿**:在 `render` 里用上 `rand_state`——每个像素采样 N 次,每次把光线方向按随机小数偏移(±半个像素),平均 N 个颜色。`curand_uniform(&state)` 取出 [0,1) 随机数。
- **漫反射/材质**:命中球后不直接返回法线色,而是沿法线半球随机反射,递归追踪 → 软阴影、颜色渗透。
- 文档学习路线:继续读 2.2(硬件实现)、异步执行章节(stream/event)、共享内存章节——后两者是 `__syncthreads` 与性能优化的主场。

---

*完。建议复习顺序:先通读第 1~8 节(文档核心 + 项目主线),再看第 15 节(项目专属细节),最后用第 16 节两张表自测一遍。*
