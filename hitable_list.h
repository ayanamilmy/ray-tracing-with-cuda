/*
hit_record结构体包含三个变量，一个是击中的时间t，一个是击中点的三维空间坐标，一个是击中点表面的法向量
*/


#ifndef HITABLE_LIST_H
#define HITABLE_LIST_H

#include "hitable.h"
#include "aabb.h"

class hitable_list : public hitable
{
public:
    __device__ hitable_list() {}
    __device__ hitable_list(hitable **l, int n)
    {
        list = l;
        list_size = n;
    }
    __device__ virtual bool hit(const ray &r, float t_min, float t_max, hit_record &rec) const;
    // 新接口:把列表里所有物体占的空间合并成一个大盒子(接口零件,已替你写好)
    __device__ virtual bool bounding_box(aabb &output_box) const
    {
        if (list_size == 0)
            return false;
        aabb temp;
        bool first = true;
        for (int i = 0; i < list_size; i++)
        {
            if (!list[i]->bounding_box(temp))
                return false;
            output_box = first ? temp : surrounding_box(output_box, temp);
            first = false;
        }
        return true;
    }
    hitable **list;
    int list_size;
};
//以下是hit函数的实现
__device__ bool hitable_list ::hit(const ray &r, float t_min, float t_max, hit_record &rec) const
{
    hit_record temp_rec;
    bool hit_anything = false;
    float closet_so_far = t_max;
    for (int i = 0; i < list_size;i++)
    {
        if(list[i]->hit(r,t_min,closet_so_far,temp_rec))
        {
            hit_anything = true;
            closet_so_far = temp_rec.t;
            rec = temp_rec;
        }
    }
    return hit_anything; // bool类型函数，返回一个bool
}
#endif