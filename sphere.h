#ifndef SPHERE_H
#define SPHERE_H

#include "hitable.h"
#include "material.h"
#include "aabb.h"
class sphere : public hitable
{
public:
    vec3 center;
    float radius;//半径
    material *mat_ptr; // 新增：这个球表面裹的材料（nullptr 表示没裹）
    __device__ sphere(){ mat_ptr = nullptr; }
    __device__ sphere(const vec3 &c,const float r, material *m = nullptr)
    {
        center=c;
        radius = r;
        mat_ptr = m; // 默认 nullptr：不传材料时，旧代码完全不受影响
    }
    // 球占的空间：中心 ± 半径的正方体（接口零件，已替你写好）
    __device__ virtual bool bounding_box(aabb &output_box) const
    {
        output_box = aabb(center - vec3(radius, radius, radius),
                          center + vec3(radius, radius, radius));
        return true;
    }
    __device__ virtual bool hit(const ray &r,float t_min,float t_max,hit_record &rec) const
    {
        vec3 oc = r.origin() - center;
        float a = dot(r.direction(), r.direction());
        float b = 2.0f * dot(oc, r.direction());
        float c = dot(oc, oc) - radius * radius;
        float discriminant = b * b - 4 * a * c; // 计算判别式
        if (discriminant > 0)
        {
            float sq = sqrt(discriminant); // 只开一次根号，存起来复用

            float T_1 = (-b - sq) / (2.0f * a);
            if (T_1 >= t_min && T_1 <= t_max)
            {
                rec.t = T_1;
                rec.p = r.point_at_parameter(rec.t);
                rec.normal = (rec.p - center) / radius;
                rec.mat_ptr = mat_ptr; // 把材料写进记录：color() 拿到记录才知道该问谁
                return true;
            }//记录一下打中的情况

            float T_2 = (-b + sq) / (2.0f * a);
            if (T_2 >= t_min && T_2 <= t_max)
            {
                rec.t = T_2;
                rec.p = r.point_at_parameter(rec.t);
                rec.normal = (rec.p - center) / radius;
                rec.mat_ptr = mat_ptr; // 把材料写进记录：color() 拿到记录才知道该问谁
                return true;
            } // 记录一下打中的情况
        }

        return false; //
    }
};
#endif