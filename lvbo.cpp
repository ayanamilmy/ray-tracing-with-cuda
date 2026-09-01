#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

// 1. 定义高斯函数
// 公式: exp( -(x^2) / (2 * sigma^2) )
float gaussian(float x, float sigma)
{
    return std::exp(-(x * x) / (2.0f * sigma * sigma));
}

// 2. 双边滤波基础核心函数
void bilateralFilter(const std::vector<std::vector<float>> &input,
                     std::vector<std::vector<float>> &output,
                     int width, int height,
                     float sigma_s, float sigma_r)
{

    // 经典优化：将搜索邻域限制在 2倍的 sigma_s 半径内 (依据论文 Section 4.1)
    int radius = std::ceil(2.0f * sigma_s);

    // 外层双循环：遍历图像中的每一个目标像素 p (坐标为 x, y)
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {

            float pixel_sum = 0.0f;  // 记录加权后的像素值总和
            float weight_sum = 0.0f; // 记录权重总和 (归一化因子 W_p)

            float current_pixel = input[y][x];

            // 内层双循环：遍历当前像素 p 附近的邻居像素 q (坐标为 nx, ny)
            for (int j = -radius; j <= radius; ++j)
            {
                for (int i = -radius; i <= radius; ++i)
                {

                    int nx = x + i;
                    int ny = y + j;

                    // 边界检查，防止数组越界
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height)
                    {

                        float neighbor_pixel = input[ny][nx];

                        // 第一部分：计算空间权重 (Spatial Weight)
                        // 获取物理距离，并输入高斯函数
                        float spatial_dist = std::sqrt(i * i + j * j);
                        float spatial_weight = gaussian(spatial_dist, sigma_s);

                        // 第二部分：计算值域权重 (Range Weight)
                        // 获取像素亮度差绝对值，并输入高斯函数
                        float range_diff = std::abs(current_pixel - neighbor_pixel);
                        float range_weight = gaussian(range_diff, sigma_r);

                        // 第三部分：总权重相乘
                        float total_weight = spatial_weight * range_weight;

                        // 第四部分：累加
                        pixel_sum += neighbor_pixel * total_weight;
                        weight_sum += total_weight;
                    }
                }
            }
            // 第五部分：归一化处理，得出最终平滑且保边的结果
            output[y][x] = pixel_sum / weight_sum;
        }
    }
}

// ================= 测试用例 =================
int main()
{
    int width = 5;
    int height = 5;

    // 创建一个简单的 5x5 灰度图，中间包含一个"硬边缘" (0 和 255 交界)
    std::vector<std::vector<float>> image = {
        {10, 12, 10, 250, 255},
        {12, 11, 15, 255, 253},
        {10, 9, 12, 248, 250},
        {13, 10, 11, 253, 255},
        {11, 12, 10, 250, 249}};

    std::vector<std::vector<float>> output(height, std::vector<float>(width, 0.0f));

    // 设置参数：
    // sigma_s = 2.0 (看周围几个像素)
    // sigma_r = 50.0 (容忍最大 50 左右的色差，色差超过 50 则完全不平滑)
    float sigma_s = 2.0f;
    float sigma_r = 50.0f;

    bilateralFilter(image, output, width, height, sigma_s, sigma_r);

    // 打印输出结果，你会发现：左侧(10左右)平滑了，右侧(250左右)平滑了，但它们之间的强边缘没有被模糊！
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            std::cout << output[y][x] << "\t";
        }
        std::cout << "\n";
    }

    return 0;
}