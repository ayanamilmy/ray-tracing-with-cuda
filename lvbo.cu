#include <iostream>
#include <math.h>

/* =====================================================================
 * lvbo.cu —— 双边滤波的 GPU 版(和 lvbo.cpp 的 CPU 版一一对应)
 *
 * 验收铁律(项目老规矩):GPU 输出必须和 CPU 版逐个数一致,
 *   同一张 5×5 边沿图:左边平滑到 ~11,右边平滑到 ~252,中间边缘锐利。
 *
 * 结构:CPU 版的两个外层循环(每个输出像素)换成"一个工人负责一个像素",
 *       内层循环(邻居窗口)原样照抄;图像从 vector<vector> 变成一维连续数组。
 * ===================================================================== */

// 1. 高斯函数(GPU 版:std::exp 换成 expf)
// TODO 核心代码①:__device__ 版 gaussian(float x, float sigma),数学和 lvbo.cpp 一模一样

__device__ float gaussian(float x, float sigma)
{
    return expf(-(x * x) / (2.0f * sigma * sigma));
}
// 2. 双边滤波核函数
// TODO 核心代码②:
//   __global__ void bilateral_gpu(const float *input, float *output,
//                                 int width, int height, float sigma_s, float sigma_r)
//   骨架照抄 main_accum.cu 里的 blur:i、j 从线程号算出来,越界 return,
//   pixel_index = j * width + i。
//   中间照抄 lvbo.cpp 的内层双循环,注意三处翻译:
//     - 图像是"平"的:邻居地址 = ny * width + nx(不再有 input[ny][nx])
//     - 邻居越界(走出图像)→ continue(照抄 CPU 版的条件判断)
//     - 最后 output[pixel_index] = pixel_sum / weight_sum(除以权重和,不是邻居个数!)
//   用到的数学函数:sqrt → sqrtf,exp → expf(GPU 上不能直接用 std::exp)

__global__ void bilateral_GPU(const float *input, float *output, int width, int height, float sigma_s, float sigma_r)
{
    int i = threadIdx.x + blockDim.x * blockIdx.x;
    int j = threadIdx.y + blockDim.y * blockIdx.y;
    if (i >= width || j >= height)
        return;
    int radius = (int)ceilf(2.0f * sigma_s); // 半径 = ceil(2*sigma_s),照抄 CPU 版
    float pixel_sum = 0.0f;  // 记录加权之后的像素值之和
    float weight_sum = 0.0f; // 权重总和
    int pixel_index = j * width + i;
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            int nx = x + i;
            int ny = y + j;
            if (nx >= 0 && nx < width && ny >= 0 && ny < height)
            {
                int neibor_pixel = ny * width + nx;                     // 把半径内部的邻居坐标算出来
                float spatial_dist = sqrtf(x * x + y * y);              // 距离公式
                float spatial_weight = gaussian(spatial_dist, sigma_s); // 计算空间权重
                // 第二部分：计算值域权重 (Range Weight)
                // 获取像素亮度差绝对值，并输入高斯函数
                float range_diff = abs(input[pixel_index] - input[neibor_pixel]);
                float range_weight = gaussian(range_diff, sigma_r);

                float total_weight = spatial_weight * range_weight;
                pixel_sum += input[neibor_pixel] * total_weight;
                weight_sum += total_weight;
            }
        }
    }
    output[pixel_index] = pixel_sum / weight_sum;
}
using namespace std;
int main()
{
    int width = 5, height = 5;
    // 和 lvbo.cpp 同一张图,一行一行摊平(第 0 行放最前)
    float image[25] = {
        10, 12, 10, 250, 255,
        12, 11, 15, 255, 253,
        10, 9, 12, 248, 250,
        13, 10, 11, 253, 255,
        11, 12, 10, 250, 249};
    float sigma_s = 2.0f;
    float sigma_r = 50.0f;

    float *d_input, *d_output;
    cudaMalloc((void **)&d_input, width * height * sizeof(float));
    cudaMalloc((void **)&d_output, width * height * sizeof(float));
    cudaMemcpy(d_input, image, width * height * sizeof(float), cudaMemcpyHostToDevice);

    // TODO 核心代码③:发射核函数
    //   25 个像素,选你喜欢的队形,工人编号公式要跟着队形配:
    //   方案 A:1 个 block、25 线程,一维 → i = threadIdx.x % width, j = threadIdx.x / width
    //   方案 B:1 个 block、dim3(5,5) → i = threadIdx.x, j = threadIdx.y
    //   (真实图像里用的是 render 那种 blocks×threads 大军,这里先小练)
    // bilateral_gpu<<<...>>>(d_input, d_output, width, height, sigma_s, sigma_r);
    bilateral_GPU<<<1, dim3(5, 5)>>>(d_input, d_output, width, height, sigma_s, sigma_r);
    float output[25];
    cudaMemcpy(output, d_output, width * height * sizeof(float), cudaMemcpyDeviceToHost);

    // TODO 核心代码④:打印结果(5×5 方阵),和 CPU 版对比验收
    for (int i = 0; i < 25; i++)
    {
        cout << output[i] << "\t";
        if((i+1)%5==0)
            cout << endl;
    }
    cudaFree(d_input);
    cudaFree(d_output);
    return 0;
}
