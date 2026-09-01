#ifndef AABB_H
#define AABB_H

#include "vec3.h"
#include "ray.h"

/* =====================================================================
 * aabb.h —— 包围盒(把你 CPU 版的 aabb.h 移植到 GPU)
 *
 * 一个盒子 = "最小角" + "最大角"两个点,框住一块长方体空间。
 * 它是 BVH 的筛子:光线先问盒子,穿过了才值得进去看球。
 * ===================================================================== */
struct aabb
{
    vec3 minimum;
    vec3 maximum;

    __device__ aabb() {}
    __device__ aabb(const vec3 &a, const vec3 &b)
    {
        minimum = a; /* maximum是指的是这个盒子坐标最大的那个点 */

        maximum = b; /* maximum是指的是这个盒子坐标最大的那个点 */
    }

    /* TODO 核心代码⑫a:盒子测光线(slab 法)——把你 CPU 版 hit 原样搬过来,double 全换 float
     * 1. 循环三个轴 a = 0,1,2:
     *    float invD = 1.0f / r.direction()[a];           —— 速度的倒数(除法贵,存倒数以后全用乘法)
     *    float t0 = (minimum[a] - r.origin()[a]) * invD; —— 到 min 面的时间
     *    float t1 = (maximum[a] - r.origin()[a]) * invD; —— 到 max 面的时间
     * 2. if (invD < 0) 交换 t0、t1   —— 朝负方向走,先到的是 max 面,掰正成"进/出"
     * 3. t_min = t0 > t_min ? t0 : t_min;  —— 进门时刻取最晚(最后一个门进去)
     *    t_max = t1 < t_max ? t1 : t_max;  —— 出门时刻取最早(第一个门出来)
     * 4. if (t_max <= t_min) return false; —— 还没进门就出门了 → 没打中
     * 5. 三个轴都过 → return true */
    __device__ bool hit(const ray &r, float t_min, float t_max) const
    {
        for (int a = 0; a <= 2;a++)
        {
            float invD = 1.0f / r.direction()[a];/* 速度的倒数 */
            float t0 = (minimum[a] - r.origin()[a]) * invD;
            float t1 = (maximum[a] - r.origin()[a]) * invD;
            if(t0>t1){
                float temp;
                temp = t0;
                t0 = t1;
                t1 = temp;
            }
            t_min = t0 > t_min ? t0 : t_min; // —— 进门时刻取最晚(最后一个门进去)
            t_max = t1 < t_max ? t1 : t_max; // —— 出门时刻取最早(第一个门出来)
            if(t_max<=t_min)
                return false;
        }
        return true;
    }

    /* TODO 核心代码⑭b(上):盒测的"带进门时间"变体 —— 照抄你 ⑫a 的 hit,只加一样东西
     * 签名:__device__ bool hit_entry(const ray &r, float t_min, float t_max, float &t_entry) const
     * 做法:把上面 hit() 的循环原样搬过来,唯一区别:
     *       循环里那个 t_min 一直在当"进门时间累加器"(每次都取 t0 和它的较大者),
     *       循环跑完,它就是"最后一个门进来的时刻"= 光线进盒的时刻。
     *       命中时写 t_entry = t_min;(就是循环里那个 t_min!)再 return true;
     *       没命中 return false(t_entry 不用管)。
     * 为什么进门时间就是它:每个轴都有进/出两个面,进门要三个轴的门都进过,
     *       所以进门时刻 = 三个"进"里最晚的那个 —— 这正是循环里 t_min 存的数。
     * (t_entry 是引用参数 = 把结果装进调用方给的盒子里,和 bounding_box 的 output_box 一个道理)
     */
    __device__ bool hit_entry(const ray &r, float t_min, float t_max, float &t_entry) const
    {
        for (int a = 0; a <= 2; a++)
        {
            float invD = 1.0f / r.direction()[a]; /* 速度的倒数 */
            float t0 = (minimum[a] - r.origin()[a]) * invD;
            float t1 = (maximum[a] - r.origin()[a]) * invD;
            if (t0 > t1)
            {
                float temp;
                temp = t0;
                t0 = t1;
                t1 = temp;
            }
            t_min = t0 > t_min ? t0 : t_min; // —— 进门时刻取最晚(最后一个门进去)
            t_max = t1 < t_max ? t1 : t_max; // —— 出门时刻取最早(第一个门出来)
            if (t_max <= t_min)
                return false; // 这个轴还没进门就出门了 → 没打中(和 hit() 一样)
        }
        t_entry = t_min; /* 三个轴的门都验完才走到这一行;最晚进门时刻一直攒在 t_min 里 */
        return true;
    }
};

/* 两个小盒子合成一个大盒子:各自挑最小的最小角、最大的最大角(把你 CPU 版 surrounding_box 搬过来)
 * 提示:float 版本用 fminf/fmaxf(CPU 版是 double 的 fmin/fmax) */
inline __device__ aabb surrounding_box(aabb box0, aabb box1)
{
    aabb box2;
    float x = box0.minimum.x() < box1.minimum.x() ? box0.minimum.x() : box1.minimum.x();
    float y = box0.minimum.y() < box1.minimum.y() ? box0.minimum.y() : box1.minimum.y();
    float z = box0.minimum.z() < box1.minimum.z() ? box0.minimum.z() : box1.minimum.z();

    float x_1 = box0.maximum.x() > box1.maximum.x() ? box0.maximum.x() : box1.maximum.x();
    float y_1 = box0.maximum.y() > box1.maximum.y() ? box0.maximum.y() : box1.maximum.y();
    float z_1 = box0.maximum.z() > box1.maximum.z() ? box0.maximum.z() : box1.maximum.z();
    box2.minimum = vec3(x, y, z);
    box2.maximum = vec3(x_1, y_1, z_1);
    return box2;
}

#endif
