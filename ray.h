#ifndef RAYH
#define RAYH

#include "vec3.h" // 光线的起点和方向都需要用到 vec3

class ray
{
public:
    // 构造函数：一定要加 __host__ __device__
    __host__ __device__ ray() {}
    __host__ __device__ ray(const vec3 &a, const vec3 &b)
    {
        A = a;
        B = b;
    }

    // 获取起点和方向
    __host__ __device__ vec3 origin() const { return A; }
    __host__ __device__ vec3 direction() const { return B; }

    // 核心公式：P(t) = A + t*B
    __host__ __device__ vec3 point_at_parameter(float t) const { return A + t * B; }

    vec3 A;
    vec3 B;
};

#endif