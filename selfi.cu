#include <cuda_runtime_api.h>
#include <memory.h>
#include <cstdlib>
#include <ctime>
#include <stdio.h>
#include <cuda/cmath>
#include <iostream>
using namespace std;
__global__ void GPUversion_arrplus(float *a, float *b, float *c, int array_length)
{
    int i = threadIdx.x + blockDim.x * blockIdx.x;
    if (i > array_length)
        return;
    else
        c[i] = a[i] + b[i];
}
void CPUversion_arrplus(float *a, float *b, float *c, int length)
{
    for (int i = 0; i < length; i++)
    {
        c[i] = a[i] + b[i];
    }
}
void explictMemExample(int array_length)
{
    // 内存中的指针
    float *a = nullptr;
    float *b = nullptr;
    float *c = nullptr;
    float *comparisonResult = (float *)malloc(array_length * sizeof(float));
    // 显存中的指针
    float *devA = nullptr;
    float *devB = nullptr;
    float *devC = nullptr;
    // 分配内存空间
    cudaMallocHost(&a, array_length * sizeof(float));
    cudaMallocHost(&b, array_length * sizeof(float));
    cudaMallocHost(&c, array_length * sizeof(float));
    // 分配显存空间
    cudaMalloc(&devA, array_length * sizeof(float));
    cudaMalloc(&devB, array_length * sizeof(float));
    cudaMalloc(&devC, array_length * sizeof(float));
    // 搬运数据
    cudaMemcpy(devA, a, array_length * sizeof(float), cudaMemcpyDefault);
    cudaMemcpy(devB, b, array_length * sizeof(float), cudaMemcpyDefault);
    cudaMemset(devC, 0, array_length * sizeof(float));
    // 准备启动核函数
    int threads = 256;
    int blocks = cuda::ceil_div(array_length, threads);
    GPUversion_arrplus<<<blocks, threads>>>(devA, devB, devC, array_length);
    cudaDeviceSynchronize(); // 防止异步执行，让CPU等一下
}

int main() {}