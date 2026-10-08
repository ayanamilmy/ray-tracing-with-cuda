/*
hitable这个基类里面只有一个函数，是bool类型的，看看有没有击中而已
*/
#ifndef HITABLE_H
#define HITABLE_H

#include "ray.h"

class material; // 前置声明：hit_record 里只存材料指针，不必知道材料的完整定义
struct aabb;    // 前置声明：包围盒（真正定义在 aabb.h）

// 击中记录结构体：相当于光线的“行车记录仪”
// 当光线击中物体时，把击中的细节（时间、地点、法向量）打包存放在这里
struct hit_record
{
    float t;     // 击中的时间（距离参数）
    vec3 p;      // 击中点的三维空间坐标
    vec3 normal; // 击中点表面的法向量
    material *mat_ptr; // 新增：打中的物体表面裹着什么材料
    vec3 geometric_normal = vec3(0, 0, 0);
    float u = 0.0f, v = 0.0f;
    bool has_uv = false;
};

// 抽象基类：世界上所有可以被光线击中的物体的“老祖宗”
class hitable
{
public:
    // BVH 通过 hitable* 删除球和三角形,需要虚析构以正确销毁派生对象。
    __device__ virtual ~hitable() {}
    // 👇 极其关键：这是会在 GPU 显存里动态调用的虚函数，必须贴上 __device__ 通行证！
    // = 0 表示这是纯虚函数，强迫它的子类（比如球、列表）必须自己实现击中逻辑
    __device__ virtual bool hit(const ray &r, float t_min, float t_max, hit_record &rec) const = 0;
    // 新接口：交出自己占的空间（建树、合并盒子全靠它）
    __device__ virtual bool bounding_box(aabb &output_box) const = 0;
};

#endif
