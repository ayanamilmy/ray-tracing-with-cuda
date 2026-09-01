#ifndef CAMERA_H
#define CAMERA_H
/* =====================================================================
 * camera.h —— 相机(把你 CPU 版的 cemara.h 移植到 GPU)
 *
 * 相机 = 眼睛 + 一块虚拟屏幕。
 * 构造函数是"组装车间":把"眼睛摆哪、屏幕多大、对焦在哪"算成几个零件存起来;
 * get_ray 每次回答"穿过屏幕上 (s,t) 位置的光线长什么样"。
 *
 * 注意:get_ray 跑在 GPU 上要 __device__;构造函数要 __host__ __device__,
 * 因为 main 在 CPU 上造相机、按值传给核函数(和 vec3 按值传参同一个道理)。
 * ===================================================================== */
#include "ray.h"
#include "random.h" /* random_in_unit_disk 在这里,闲置已久的它终于要上岗了 */
#include <math.h>

class camera
{
public:
    __host__ __device__ camera(
        vec3 lookfrom,    // 相机摆在哪(你的眼睛在哪)
        vec3 lookat,      // 盯着哪看(目标点)
        vec3 vup,         // 哪边是头顶(通常 (0,1,0),控制歪脖子)
        float vfov,       /* 垂直视角(度):越大越广角 */
        float aspect,     // 屏幕宽高比(我们是 1000/500 = 2)
        float aperture,   /* 光圈:0 = 针孔(绝对清晰),越大背景虚化越狠 */
        float focus_dist) /* 对焦距离:这个距离上的物体绝对清晰 */
    {
        /* TODO 核心代码⑨a:相机的"组装车间"(把你 CPU 版一字不差搬过来,double 全换 float)
         * 1. origin = lookfrom;   lens_radius = aperture / 2.0f;
         * 2. 虚拟屏幕尺寸(初中三角函数):
         *    float theta = vfov * 3.1415926f / 180.0f;    —— 角度变弧度
         *    float half_height = tan(theta / 2.0f);       —— 对边/邻边
         *    float half_width = aspect * half_height;
         * 3. 相机本地坐标系(前/右/上,和 CPU 版完全一样):
         *    w = unit_vector(lookfrom - lookat);   —— 相机"正后方"
         *    u = unit_vector(cross(vup, w));       —— 相机"正右方"
         *    v = cross(w, u);                      —— 相机"正上方"
         * 4. 屏幕左下角:从眼睛出发,往前推 focus_dist,再往左/往下退半个屏幕:
         *    lower_left_corner = origin - half_width*focus_dist*u - half_height*focus_dist*v - focus_dist*w;
         * 5. 屏幕总宽总高(向量形式):
         *    horizontal = 2.0f * half_width  * focus_dist * u;
         *    vertical   = 2.0f * half_height * focus_dist * v;
         * (零件算好就存进下面的成员变量) */
        origin = lookfrom;
        lens_radius = aperture / 2.0f;
        float theta = vfov * 3.1415926f / 180.0f;
        float half_height = tan(theta / 2.0f);
        float half_width = aspect * half_height;
        w = unit_vector(lookfrom - lookat); /* w 轴：相机的“正后方”（因为传统图形学光线往 -Z 射，所以 w 指向后方） */
        u = unit_vector(cross(vup, w));     /* u 轴：相机的“正右方”（用 上方 叉乘 后方 得到 右方） */
        v = cross(w, u);                    /* v 轴：相机的“正上方”（用 后方 叉乘 右方 得到 真正的上方） */
        lower_left_corner = origin - half_width * focus_dist * u /* 往左移 */
                            - half_height * focus_dist * v       /* 往下移 */
                            - focus_dist * w;                    /* 往前推到对焦平面上 */
        horizontal = 2 * half_width * focus_dist * u;            /* 横向总向量 */
        vertical = 2 * half_height * focus_dist * v;             /* 纵向总向量 */
        }


    __device__ ray get_ray(float s, float t, curandState *local_rand_state) const
    {
        /* TODO 核心代码⑨b:出光线(景深的全部秘密在这 3 行)
         * 1. rd = lens_radius * random_in_unit_disk(local_rand_state);
         *    —— 在镜头圆盘上随机挑一个起点(这就是当年写圆盘函数的用途!)
         * 2. offset = u * rd.x() + v * rd.y();
         *    —— 把圆盘上的点换算到相机的"右/上"坐标系里
         * 3. return ray(origin + offset,
         *               lower_left_corner + s*horizontal + t*vertical - origin - offset);
         *    —— 起点:镜头随机点;方向:瞄向屏幕上 (s,t)。CPU 版去掉时间戳,其余一字不差 */
        // 景深偏移计算（模拟光线通过一个有物理大小的镜头）
        /* random_in_unit_disk() 会在镜头圆盘上随机挑一个点出发，产生景深虚化 */
        vec3 rd = lens_radius * random_in_unit_disk(local_rand_state);
        vec3 offset = u * rd.x() + v * rd.y(); /* 把圆盘上的随机点，转换到相机的 u, v 坐标系里 */
        return ray(
            origin + offset,                                                     /* 起点：不再是完美的相机中心 origin，而是镜头上发生偏移的一个点 */
            lower_left_corner + s * horizontal + t * vertical - origin - offset /* 方向：目标像素点 减去 偏移后的起点 */
        );
    }


public:
    vec3 origin;            /* 相机位置 */
    vec3 lower_left_corner; /* 虚拟屏幕左下角 */
    vec3 horizontal;        /* 屏幕横向总向量 */
    vec3 vertical;          /* 屏幕纵向总向量 */
    vec3 u, v, w;           /* 相机的 右/上/后 三个轴 */
    float lens_radius;      /* 镜头半径 */
};
#endif
