/* =====================================================================
 * random.h —— 随机工具 + 三个光学公式(M1 要亲手写的数学)
 *
 * 随机数:一律用 curand_uniform(local_rand_state) 取 [0,1) 之间的数,
 * 状态由 render 里的每线程状态传进来(你已有的 render_init 已经种好了种子)。
 *
 * 每个函数都是"占位版":返回一个不会崩的值,保证项目随时能编译。
 * 你按 TODO 编号亲手填,填完一个画面就变一次。
 * ===================================================================== */
#ifndef RANDOM_H
#define RANDOM_H

#include "vec3.h"
#include <curand_kernel.h>

/* TODO 核心代码②:单位球内的随机点(拒绝采样)
 * 思路:反复取 p = 2*(r1,r2,r3) - (1,1,1),即落在 [-1,1]^3 立方体里的随机点,
 *       直到它同时落在单位球内(dot(p,p) < 1)才返回。
 *       这样得到的方向在各个方向上是均匀的,漫反射才均匀。 */
__device__ vec3 random_in_unit_sphere(curandState *local_rand_state)
{
    /* 你的算法,原样搬过来(只加 __device__,删掉没用到的 direction 变量) */
    float x, y, z;
    do
    {
        x = 2.0f * curand_uniform(local_rand_state) - 1.0f;
        y = 2.0f * curand_uniform(local_rand_state) - 1.0f;
        z = 2.0f * curand_uniform(local_rand_state) - 1.0f;
    } while (x * x + y * y + z * z >= 1.0f);
    return vec3(x, y, z);
}

/* TODO 核心代码⑥:单位圆盘内的随机点(景深镜头用)
 * 思路:和上面一样,但只在 xy 平面上做:反复取 p = 2*(r1,r2,0) - (1,1,0),
 *       直到 dot(p,p) < 1。 */
__device__ vec3 random_in_unit_disk(curandState *local_rand_state)
{
    vec3 p;
    /* 【批改】✓ 条件改对了(球外才重摇,上次说反的已经纠正)。
     * 小清理:上面这行只用到 z,改成 `float z = 0.0f;` 就能消掉编译时那两个
     * "declared but never referenced" 警告。 */
    float x, y, z = 0.0f;
    do
    {
        p = vec3(2.0f * curand_uniform(local_rand_state) - 1.0f, 2.0f * curand_uniform(local_rand_state) - 1.0f, z);
    } while (dot(p, p) >= 1);

    return p; /* 占位 */
}

/* TODO 核心代码③:镜面反射
 * 公式:v 是入射方向,n 是法线,反射方向 = v - 2 * dot(v, n) * n */
__device__ vec3 reflect(const vec3 &v, const vec3 &n)
{
    /* 【批改】✓ 完全正确,一次写对。
     * 想更简洁可以一行:return v - 2.0f * dot(v, n) * n;
     * 提醒:公式前提是 v 是单位向量——调用方(metal/dielectric)要包 unit_vector(见 material.h 批改)。 */
    vec3 ans;
    ans = v - 2 * dot(v, n) * n;
    return ans;
}

/* TODO 核心代码④:折射(斯涅尔定律)
 * 公式:折射方向 = ni_over_nt * (v - n * dot(v, n)) - n * sqrt(1 - ni_over_nt^2 * (1 - dot(v,n)^2))
 *       当根号内为负 = 全反射,返回 false(光线出不去,只能反射) */
__device__ bool refract(const vec3 &v, const vec3 &n, float ni_over_nt, vec3 &refracted)
{
    /* 【批改】✓ 数学全对!三处全中:^ 换成连乘、根号内为负判全反射、算完写进 refracted 并
     * return true。上次埋的雷全排了,进步肉眼可见。
     * ⚠ 两个改进建议:
     *   1. dot(v,n) 算了三遍——提出来存变量,顺便更易读:
     *      float dt = dot(v, n);
     *      float disc = 1.0f - ni_over_nt * ni_over_nt * (1.0f - dt * dt);
     *      if (disc < 0) return false;  然后 sqrt(disc) 复用。
     *   2. 公式前提是 v 是单位向量——调用方(dielectric)传进来的没归一化,调用处要包 unit_vector。 */
    vec3 ans;
    if (1 - ni_over_nt * ni_over_nt * (1 - dot(v, n) * dot(v, n)) < 0)
        return false;//如果是全反射，那么不发生折射
    ans = ni_over_nt * (v - n * dot(v, n)) - n * sqrt(1 - ni_over_nt * ni_over_nt * (1 - dot(v, n) * dot(v, n)));
    refracted = ans;
    return true; 
}

/* TODO 核心代码④:Schlick 近似 —— 玻璃的反射率随角度变化
 * 思路:垂直看玻璃几乎全透,越斜着看反射越强(湖面远处像镜子就是这个道理)。
 * 公式:R0 = ((1 - ref_idx) / (1 + ref_idx))^2
 *       R  = R0 + (1 - R0) * (1 - cos)^5 */
__device__ float schlick(float cosine, float ref_idx)
{
    /* 【批改】✗ R0 算错了:公式是 R0 = ((1-ref_idx)/(1+ref_idx))² —— 平方!
     * 你写成了 ((1-ref_idx)/(1+ref_idx)) * (1+ref_idx),乘的是 (1+ref_idx) 而不是再乘
     * 自己一次,一约分 R0 就变成 (1-ref_idx),完全不是平方(比如 ref_idx=1.5 会算出负数)。
     * 改法:R0 = ((1 - ref_idx) / (1 + ref_idx)) * ((1 - ref_idx) / (1 + ref_idx));
     * ✓ (1-cosine) 的五次方用五个连乘写,虽然啰嗦但正确。
     * ⚠ 小提醒:cosine 传进来前要配合翻面法线保证是正数(详见 material.h 批改⑥)。 */
    float R0,R;
    R0 = ((1 - ref_idx) / (1 + ref_idx) * (1 - ref_idx) / (1 + ref_idx));
    R = R0 + (1 - R0) * (1 - cosine) * (1 - cosine) * (1 - cosine) * (1 - cosine) * (1 - cosine);
    return R; 
}

#endif
