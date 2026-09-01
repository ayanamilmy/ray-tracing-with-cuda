/*
 * myself.cu —— 自己动手:一个例子 = 一个知识点
 *
 * 编译: nvcc myself.cu -o myself.exe -Xcompiler /utf-8
 * 运行: .\myself.exe      → 默认跑例 1
 *       .\myself.exe N    → 跑第 N 个例子(N = 1~5)
 *
 * 例1:GPU 数组乘法(显式内存)—— CUDA 版 Hello World
 * 例2:GPU 数组乘法(统一内存)—— 和例1对比:哪些代码消失了?
 * 例3:二维像素映射 —— dim3、i/j 两个方向、二维越界检查
 * 例4:指针的指针 + 设备端 new —— 对应你 main.cu 里 create_world 的机制
 * 例5:错误检查 —— 故意犯错,看 CUDA 怎么报错
 *
 * 每个例子互相独立,exampleN() 可以单独抄走。注释比代码多,慢慢读。
 */

#include <stdio.h>
#include <stdlib.h>
#include <cuda_runtime.h>

/* 共用的错误检查宏(为什么这样写在例5细讲) */
#define CUDA_CHECK(expr)                                           \
    do                                                             \
    {                                                              \
        cudaError_t _err = (expr);                                 \
        if (_err != cudaSuccess)                                   \
        {                                                          \
            fprintf(stderr, "CUDA 错误: %s:%d -> %s\n",            \
                    __FILE__, __LINE__, cudaGetErrorString(_err)); \
            exit(1);                                               \
        }                                                          \
    } while (0)

/* =====================================================================
 * 例1:GPU 数组乘法(显式内存)—— CUDA 版 Hello World
 *
 * 知识点:
 *   1. __global__ 声明 kernel(kernel 必须返回 void,结果通过内存传出来)
 *   2. <<<块的个数, 每块线程数>>> 启动语法
 *   3. 全局编号公式:i = threadIdx.x + blockIdx.x * blockDim.x
 *   4. 越界检查:if (i < n) —— 多出来的线程提前下班
 *   5. 天花板除法算块数:(n + threads - 1) / threads
 *   6. 显式内存三件套:cudaMalloc(显存)、cudaMemcpy(过桥)、cudaDeviceSynchronize(等待)
 * ===================================================================== */
__global__ void vecMul(const float *a, const float *b, float *c, int n)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x; /* 我是第几号工人 */
    if (i < n)                                     /* 没我的活就下班 */
        c[i] = a[i] * b[i];
}

void example1()
{
    printf("===== 例1:GPU 数组乘法(显式内存) =====\n");
    const int n = 10; /* 故意用 10:不是 256 的倍数,专门考验越界检查 */
    size_t bytes = n * sizeof(float);

    /* 第1步:主机内存(C 语言的 malloc,数据住在 CPU 这边) */
    float *a = (float *)malloc(bytes);
    float *b = (float *)malloc(bytes);
    float *c = (float *)malloc(bytes); /* c 用来收 GPU 算完的结果 */
    for (int i = 0; i < n; i++)
    {
        a[i] = (float)(i + 1);
        b[i] = 10.0f;
    }

    /* 第2步:显存(cudaMalloc,数据住在 GPU 那边;这些指针 CPU 只能存着,不能解引用) */
    float *d_a, *d_b, *d_c;
    CUDA_CHECK(cudaMalloc((void **)&d_a, bytes));
    CUDA_CHECK(cudaMalloc((void **)&d_b, bytes));
    CUDA_CHECK(cudaMalloc((void **)&d_c, bytes));

    /* 第3步:过桥搬运:内存 → 显存 */
    CUDA_CHECK(cudaMemcpy(d_a, a, bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_b, b, bytes, cudaMemcpyHostToDevice));

    /* 第4步:启动 kernel。(10 + 256 - 1) / 256 = 1 个块,256 个线程;
     *        其中 246 个线程因为 i >= 10 提前下班 */
    int threads = 256;
    int blocks = (n + threads - 1) / threads;
    vecMul<<<blocks, threads>>>(d_a, d_b, d_c, n);

    /* 第5步:同步 —— 启动是异步的!CPU 必须站住,等 GPU 干完 */
    CUDA_CHECK(cudaDeviceSynchronize());

    /* 第6步:过桥搬回:显存 → 内存 */
    CUDA_CHECK(cudaMemcpy(c, d_c, bytes, cudaMemcpyDeviceToHost));

    /* 第7步:验证结果 */
    int ok = 1;
    for (int i = 0; i < n; i++)
    {
        printf("  c[%d] = %.0f (应为 %.0f)\n", i, c[i], a[i] * b[i]);
        if (c[i] != a[i] * b[i])
            ok = 0;
    }
    printf(ok ? "  ✓ 全部正确\n" : "  ✗ 有错误!\n");

    /* 第8步:清理 */
    cudaFree(d_a);
    cudaFree(d_b);
    cudaFree(d_c);
    free(a);
    free(b);
    free(c);
}

/* =====================================================================
 * 例2:GPU 数组乘法(统一内存)—— 和例1逐行对比
 *
 * 知识点:
 *   1. cudaMallocManaged:一块"两边都能访问"的内存,系统自动搬运
 *   2. 对比例1:没有 cudaMemcpy、没有 d_ 前缀的指针 —— 这就是统一内存省的活
 *   3. 代价:自动搬运有隐藏开销(入门阶段不用管,记个概念)
 * ===================================================================== */
__global__ void vecMul2(const float *a, const float *b, float *c, int n)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    if (i < n)
        c[i] = a[i] * b[i];
}

void example2()
{
    printf("===== 例2:GPU 数组乘法(统一内存) =====\n");
    const int n = 10;
    size_t bytes = n * sizeof(float);

    float *a, *b, *c;
    CUDA_CHECK(cudaMallocManaged(&a, bytes)); /* 一个指针,CPU 和 GPU 都能用 */
    CUDA_CHECK(cudaMallocManaged(&b, bytes));
    CUDA_CHECK(cudaMallocManaged(&c, bytes));

    for (int i = 0; i < n; i++)
    {
        a[i] = (float)(i + 1);
        b[i] = 2.0f;
    } /* CPU 直接写 */

    vecMul2<<<1, 256>>>(a, b, c, n); /* GPU 直接用同一批指针 */
    CUDA_CHECK(cudaDeviceSynchronize());

    for (int i = 0; i < n; i++) /* CPU 直接读 —— 没有任何 cudaMemcpy! */
        printf("  c[%d] = %.0f (应为 %.0f)\n", i, c[i], a[i] * b[i]);

    cudaFree(a);
    cudaFree(b);
    cudaFree(c);
}

/* =====================================================================
 * 例3:二维像素映射 —— 对应你 render / render_init 里的 i、j
 *
 * 知识点:
 *   1. dim3 表达二维的块和网格(对应你项目里的 dim3 threads(8,8))
 *   2. 两条编号公式:i(列)= threadIdx.x + blockIdx.x*blockDim.x
 *                    j(行)= threadIdx.y + blockIdx.y*blockDim.y
 *   3. 二维越界检查:行、列都要查
 *   4. 把 (i,j) 折成一维下标:pixel_index = j * nx + i(先数完前面 j 整行)
 * 输出:一张 16x7 的字符画,越亮字符越密。左上角暗、右下角亮。
 * ===================================================================== */
__global__ void pixelKernel(float *img, int nx, int ny)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x; /* 列(横向) */
    int j = threadIdx.y + blockIdx.y * blockDim.y; /* 行(纵向) */
    if (i >= nx || j >= ny)
        return; /* 越界线程下班 */

    float u = (float)i / (float)(nx - 1); /* 0 → 1,从左到右 */
    float v = (float)j / (float)(ny - 1); /* 0 → 1,从上到下 */
    img[j * nx + i] = (u + v) * 0.5f;     /* 亮度 = 位置的平均 */
}

void example3()
{
    printf("===== 例3:二维像素映射(16 x 7 字符画) =====\n");
    const int nx = 16, ny = 7; /* 宽 16、高 7 */
    const int num = nx * ny;

    float *img;
    CUDA_CHECK(cudaMallocManaged(&img, num * sizeof(float)));

    dim3 threads(4, 4);            /* 每块 4x4 = 16 个线程 */
    dim3 blocks((nx + 4 - 1) / 4,  /* 横向:16/4 = 4 块,正好 */
                (ny + 4 - 1) / 4); /* 纵向:7/4 → 2 块 = 8 行线程,多 1 行! */
    pixelKernel<<<blocks, threads>>>(img, nx, ny);
    CUDA_CHECK(cudaDeviceSynchronize());

    const char ramp[] = " .:-=+*#%@"; /* 从暗到亮共 10 档 */
    for (int j = 0; j < ny; j++)
    {
        for (int i = 0; i < nx; i++)
            printf("%c", ramp[(int)(img[j * nx + i] * 9.0f)]);
        printf("\n");
    }
    printf("  (第 7 行靠越界检查挡住了多出来的 1 行线程;试试把 ny 改成别的数)\n");

    cudaFree(img);
}

/* =====================================================================
 * 例4:指针的指针 + 设备端 new —— 对应你 main.cu 里 create_world 的机制
 *
 * 知识点:
 *   1. 在 GPU 代码里 new 对象(对象住在显存,叫"设备运行时堆")
 *   2. GPU 上也能用虚函数(多态:同一句 ask(),基类和子类算法不同)
 *   3. 为什么是"指针的指针"(Package **slot):
 *      kernel 不能返回值 → GPU new 出来的对象地址,CPU 永远不知道
 *      → CPU 自己 cudaMalloc 一个 8 字节"信箱"(信箱地址是 CPU 开的,它当然知道)
 *      → 造对象的 kernel 把对象地址写进信箱,用对象的 kernel 从信箱读出来
 *      → 信箱里装的是"地址",要让 kernel 读写信箱,交给它的必须是"信箱的地址"
 *      → "指向'装着指针的内存'的指针" = 指针的指针
 * ===================================================================== */
struct Package
{
    float v;
    __device__ Package(float v) : v(v) {}
    __device__ virtual float ask() { return v; } /* 基类算法:原样返回 */
};
struct DoublePackage : Package
{
    __device__ DoublePackage(float v) : Package(v) {}
    __device__ virtual float ask() { return v * 2.0f; } /* 子类算法:翻倍(多态!) */
};

/* 造对象的 kernel:把 new 出来的对象地址写进"信箱" *slot */
__global__ void createKernel(Package **slot)
{
    *slot = new DoublePackage(21.0f); /* 对象地址是 GPU 现场分配的,CPU 不知道 */
}

/* 用对象的 kernel:从"信箱"读出对象地址,再调用虚函数 */
__global__ void useKernel(Package **slot, float *out)
{
    *out = (*slot)->ask(); /* 拆两层:*slot = 对象地址,->ask() 虚调用 */
}

/* 拆对象的 kernel:设备端 delete 和设备端 new 成对出现 */
__global__ void deleteKernel(Package **slot)
{
    delete *slot;
}

void example4()
{
    printf("===== 例4:指针的指针 + 设备端 new =====\n");

    /* CPU 唯一"确定知道"的显存地址,就是它自己分配出来的这块 8 字节信箱 */
    Package **d_slot; /* 注意:指针的指针 */
    float *d_out;
    CUDA_CHECK(cudaMalloc((void **)&d_slot, sizeof(Package *))); /* 信箱:装 1 个地址 */
    CUDA_CHECK(cudaMalloc((void **)&d_out, sizeof(float)));      /* 结果槽 */

    createKernel<<<1, 1>>>(d_slot);     /* GPU 造对象,把对象地址写进信箱 */
    useKernel<<<1, 1>>>(d_slot, d_out); /* GPU 读信箱 → 找到对象 → 问它 */
    CUDA_CHECK(cudaDeviceSynchronize());

    float result;
    CUDA_CHECK(cudaMemcpy(&result, d_out, sizeof(float), cudaMemcpyDeviceToHost));
    printf("  结果 = %.0f (21 被虚函数翻倍 → 应为 42)\n", result);

    deleteKernel<<<1, 1>>>(d_slot); /* 拆掉对象(你项目的 free_world 忘了调用这一步) */
    CUDA_CHECK(cudaDeviceSynchronize());
    cudaFree(d_slot);
    cudaFree(d_out);
}

/* =====================================================================
 * 例5:错误检查 —— 故意犯错,看 CUDA 怎么报错
 *
 * 知识点:
 *   1. <<<>>> 启动不返回错误码 → 启动后立刻用 cudaGetLastError() 查
 *   2. 错误状态会一直"粘着",直到被读走:cudaPeekAtLastError() 只偷看不清理,
 *      cudaGetLastError() 读走并清零(场景2实测)
 *   3. kernel 执行期间的错误,要等同步(cudaDeviceSynchronize)时才能抓到
 *   4. 环境变量 CUDA_LOG_FILE 可以拿到驱动级详细日志(见文末注释)
 * ===================================================================== */
__global__ void emptyKernel() {}

void example5()
{
    printf("===== 例5:错误检查(故意犯错) =====\n");

    /* 场景1:每块 4096 线程,超过硬件上限 1024 → 启动报错 */
    printf("  场景1:启动一个每块 4096 线程的 kernel(上限是 1024):\n");
    emptyKernel<<<1, 4096>>>();
    cudaError_t e1 = cudaGetLastError(); /* 抓住启动错误 */
    printf("    cudaGetLastError() = %s\n", cudaGetErrorString(e1));

    /* 场景2:粘性 —— 错误状态不清零就会一直留着;peek 偷看不清理,get 读走并清零 */
    printf("  场景2:错误状态有粘性(再犯一次错,然后连续查三次):\n");
    emptyKernel<<<1, 4096>>>();             /* 再犯一次 */
    cudaError_t e2 = cudaPeekAtLastError(); /* 偷看:不清零 */
    printf("    第1次 cudaPeekAtLastError() = %s (偷看,不清零)\n", cudaGetErrorString(e2));
    cudaError_t e3 = cudaGetLastError(); /* 读走:并清零 */
    printf("    第2次 cudaGetLastError()   = %s (读走并清零)\n", cudaGetErrorString(e3));
    cudaError_t e4 = cudaGetLastError();
    printf("    第3次 cudaGetLastError()   = %s (清干净了)\n", cudaGetErrorString(e4));

    /* 场景3:正确的套路 —— 启动后查、同步后查 */
    emptyKernel<<<1, 256>>>();
    CUDA_CHECK(cudaGetLastError());      /* 查启动 */
    CUDA_CHECK(cudaDeviceSynchronize()); /* 查执行期错误 */
    /* 注意:✓ 和 \n 之间必须隔一个空格 —— 中文 Windows 上编译器按 GBK 读源码,
     * ✓ 的最后一个字节和反斜杠会被拼成一个双字节字符,把 \n 的转义"吞"掉(实测踩过的坑) */
    printf("  场景3:正确的启动 + 同步,全部通过 ✓ \n");
    printf("  (想抓执行期错误?让 kernel 里写 *((int*)0) = 1,再跑,看同步怎么报错)\n");

    /* 关于 CUDA_LOG_FILE(PowerShell):
     *   $env:CUDA_LOG_FILE = "cuda_log.txt
     *   .\myself.exe 5
     * 之后 cuda_log.txt 里会有驱动写的详细错误日志。 */
}

int main(int argc, char **argv)
{
    int which = 1;
    if (argc >= 2)
        which = atoi(argv[1]);

    printf("myself.cu 练习:运行例 %d\n\n", which);
    switch (which)
    {
    case 1:
        example1();
        break;
    case 2:
        example2();
        break;
    case 3:
        example3();
        break;
    case 4:
        example4();
        break;
    case 5:
        example5();
        break;
    default:
        printf("用法: .\\myself.exe N (N = 1~5),不带参数默认跑例 1\n");
    }
    return 0;
}
