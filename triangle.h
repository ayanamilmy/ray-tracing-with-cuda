#pragma once
#include "hitable.h"
#include "aabb.h"
#include "mesh_data.h"


class triangle : public hitable
{
public:
    __device__ triangle(
        const triangle_data &data,
        material *mat)
        : a_(data.a), b_(data.b), c_(data.c), mat_(mat)
    {
        has_uv_ = data.has_uv;
        has_normals_ = data.has_normals;
        for (int k = 0; k < 3; ++k) { uv_[k] = data.uv[k]; n_[k] = data.n[k]; }
        // 三个顶点在场景建立后不变，所以两条边只计算一次。
        edge1_ = b_ - a_;
        edge2_ = c_ - a_;

        // 两条边的叉乘，得到垂直于三角形表面的方向。
        const vec3 n = cross(edge1_, edge2_);

        // 只有非零向量才能归一化。
        normal_ = n.squared_length() > 0.0f
            ? unit_vector(n)
            : vec3(0, 0, 0);
    }
    __device__ bool hit(const ray &r,float t_min,float t_max,hit_record &rec) const override
    {
        const vec3 pvec=cross(r.direction(),edge2_);
        const float det=dot(edge1_,pvec);
         // 平行，或者三角形退化：无法得到正常交点。
        if(fabsf(det)<1e-8f)
        {
            return false;
        }
        const float inv_det = 1.0f / det;
        const vec3 tvec = r.origin() - a_;

        // 求出 B 顶点的权重。
        const float b1 = dot(tvec, pvec) * inv_det;

        if (b1 < 0.0f || b1 > 1.0f)
            return false;

        const vec3 qvec = cross(tvec, edge1_);

        // 求出 C 顶点的权重。
        const float b2 = dot(r.direction(), qvec) * inv_det;

        if (b2 < 0.0f || b1 + b2 > 1.0f)
            return false;

        // 求出射线前进到交点时的参数 t。
        const float t = dot(edge2_, qvec) * inv_det;

        if (t < t_min || t > t_max)
            return false;

        // 前面的检查全部通过，才填写命中记录。
        rec.t = t;
        rec.p = r.point_at_parameter(t);
        const float b0 = 1.0f - b1 - b2;
        rec.has_uv = has_uv_;
        rec.u = rec.v = 0.0f;
        if (has_uv_) {
            rec.u = b0*uv_[0].u + b1*uv_[1].u + b2*uv_[2].u;
            rec.v = b0*uv_[0].v + b1*uv_[1].v + b2*uv_[2].v;
        }
        vec3 ng = normal_, ns = ng;
        if (has_normals_) {
            const vec3 n = b0*n_[0] + b1*n_[1] + b2*n_[2];
            if (n.squared_length() > 1e-20f) ns = unit_vector(n);
            if (dot(ns, ng) < 0.0f) ns = -ns;
        }
        if (dot(r.direction(), ng) > 0.0f) { ng = -ng; ns = -ns; }
        if (dot(r.direction(), ns) >= 0.0f) ns = ng;
        rec.normal = ns;
        rec.geometric_normal = ng;

        rec.mat_ptr = mat_;
        return true;
    }
    __device__ bool bounding_box(aabb &box) const override
    {
        const float pad = 1e-4f;

        const vec3 lo(
            fminf(a_.x(), fminf(b_.x(), c_.x())) - pad,
            fminf(a_.y(), fminf(b_.y(), c_.y())) - pad,
            fminf(a_.z(), fminf(b_.z(), c_.z())) - pad);

        const vec3 hi(
            fmaxf(a_.x(), fmaxf(b_.x(), c_.x())) + pad,
            fmaxf(a_.y(), fmaxf(b_.y(), c_.y())) + pad,
            fmaxf(a_.z(), fmaxf(b_.z(), c_.z())) + pad);

        box = aabb(lo, hi);
        return true;
    }

private:
    vec3 a_, b_, c_;
    vec3 edge1_, edge2_;
    vec3 normal_;
    material *mat_;
    uv2 uv_[3];
    vec3 n_[3];
    bool has_uv_, has_normals_;

};
