/* =====================================================================
 * material.h —— 材料系统(M1 的核心新概念)
 *
 * 思路:把"形状"和"材质"拆开。
 *   hitable(球/列表)负责回答:"打中了吗?打在哪?"   —— 这是几何问题
 *   material(材料) 负责回答:"打中之后,光线怎么走?" —— 这是光学问题
 *   球还是那个球,表面裹不同的材料,性格就完全不同。
 *
 * 抽象接口 scatter(散射):
 *   输入 :入射光线 r_in、击中记录 rec、当前线程的随机数状态 local_rand_state
 *   输出 :attenuation(衰减色 —— 这层材料"吸掉"了多少光)
 *          scattered(散射出去的新光线)
 *   返回 :true  = 光线被散射,继续追踪下去
 *          false = 光线死了(能量被吸收),这条光线对颜色没有贡献(返回黑色)
 *
 * 三个子类三种散射算法 —— 虚函数多态,和 hitable::hit 完全一个套路。
 * 注意:所有函数都要 __device__,因为它们跑在 GPU 上。
 * ===================================================================== */
#ifndef MATERIAL_H
#define MATERIAL_H

#include "ray.h"
#include "hitable.h"
#include "texture.h"
#include "random.h" /* 框架已接好:scatter 里要调 random_in_unit_sphere */
#include <curand_kernel.h>

class material
{
public:
    __device__ virtual vec3 emitted(const ray &r_in,const hit_record &rec) const
    {
        return vec3 (0.0f,0.0f,0.0f);
    }
    __device__ virtual bool get_diffuse_albedo(const hit_record &rec, vec3 &out_albedo) const
    {
        return false;
    }
    __device__ virtual bool scatter(const ray &r_in, const hit_record &rec,
                                    vec3 &attenuation, ray &scattered,
                                    curandState *local_rand_state) const = 0;
};

class diffuse_light:public material
{
    public:
        vec3 emission;
        __device__ diffuse_light(const vec3 &e):emission(e) {}


    __device__ vec3 emitted(const ray &r_in,const hit_record &rec) const override
    {
        return emission;
    }
    __device__ virtual bool scatter(const ray &r_in, const hit_record &rec,
                                    vec3 &attenuation, ray &scattered,
                                    curandState *local_rand_state) const
    {
        return false;
    }







};
/* ---------- 漫反射(哑光) ----------
 * 思路:光线打到表面后,朝"法线附近"随机反弹 —— 能量被四面八方地涂匀。
 * 这是最朴素的材质:地面、墙、粗糙物体的底色。 */
class lambertian : public material
{
public:
    vec3 albedo; /* 反射率:这个材料"是什么颜色" */
    texture_view base_color_image;
    __device__ lambertian(vec3 a, texture_view image = texture_view{})
        : albedo(a), base_color_image(image) {}
    __device__ vec3 albedo_at(const hit_record &rec) const
    {
        if (!base_color_image.rgba) return albedo;
        if (!rec.has_uv) return vec3(1, 0, 1);
        return albedo * sample_base_color(base_color_image, rec.u, rec.v);
    }

    __device__ bool get_diffuse_albedo(const hit_record &rec, vec3 &out_albedo) const override
    {
        out_albedo = albedo_at(rec);
        return true;
    }

    __device__ virtual bool scatter(const ray &r_in, const hit_record &rec,
                                    vec3 &attenuation, ray &scattered,
                                    curandState *local_rand_state) const
    {
        /* TODO 核心代码②:漫反射散射(见下方注释) */
        /* 1. 目标方向 = 法线 + 单位球内的随机点(random_in_unit_sphere)
         * 2. scattered = ray(击中点 p, 目标方向)
         * 3. attenuation = albedo
         * 4. 返回 true */
        /* 【批改】✓ 全对。方向 = 法线 + 球内随机点,上次讲过的"代数合并"你用得比课本还简练。
         * 小提示:下面 return true 后面的注释还写着"占位:先保证能编译",现在已是真代码,回头顺手删掉。 */
        vec3 target = rec.normal + random_unit_vector(local_rand_state);

        if (dot(target, target) <= 1e-12f) {
            target = rec.normal;
        }
        scattered = ray(rec.p, target);
        attenuation = albedo_at(rec);
        return true; /* 占位:先保证能编译 */
    }
};

/* ---------- 金属(镜面) ----------
 * 思路:按反射定律镜面弹走,再叠加一点随机扰动。
 * fuzz 越大扰动越强 —— 0 是完美镜面,1 是磨砂金属。 */
class metal : public material
{
public:
    vec3 albedo;
    float fuzz;
    __device__ metal(vec3 a, float f) : albedo(a), fuzz(f) {}

    __device__ virtual bool scatter(const ray &r_in, const hit_record &rec,
                                    vec3 &attenuation, ray &scattered,
                                    curandState *local_rand_state) const
    {
        /* TODO 核心代码③:镜面反射(见下方注释) */
        /* 1. 反射方向 = reflect(入射方向, 法线) + fuzz * 单位球内随机点
         * 2. 若 (反射方向 · 法线) <= 0,扰动太大反射进了表面,视为被吸收 → 返回 false
         * 3. attenuation = albedo,返回 true */
        /* 【批改】metal 骨架对(reflect + fuzz 扰动 + 装 scattered + return true),但有一个致命 bug 和几个隐患:
         * ✗ 致命(第 80 行):检查的对象错了!该检查的是"弹出去的方向" target_1 有没有钻进表面:
         *     if (dot(target_1, rec.normal) <= 0) return false;
         *   你现在检查的是"入射方向 · 法线"——从外面打进来的光线,入射朝里、法线朝外,
         *   点积永远是负的 → 条件永远成立 → 金属永远 return false → 金属全黑!
         *   TODO 注释里那句"扰动太大反射进了表面"防的是出去的光,不是进来的光。
         * ⚠ 隐患(第 79 行):reflect 公式要求 v 是单位向量,要包一层:
         *   reflect(unit_vector(r_in.direction()), rec.normal)
         * ⚠ 建议:fuzz 大于 1 时在构造函数里截成 1,否则扰动会大到超出法线半球。
         * ✓ 其余两行(attenuation = albedo; scattered = ray(rec.p, target_1);)都写对了。 */
        vec3 target_1 = reflect(r_in.direction(), rec.normal)+fuzz*random_in_unit_sphere(local_rand_state);
        if(dot(unit_vector(target_1),rec.normal)<=0)
            return false;
        attenuation = albedo;
        scattered = ray(rec.p,target_1);/*给出反射光线的起点和方向 */
        return true; /* 占位:先保证能编译 */
    }
};

/* ---------- 玻璃 / 水(透明介质) ----------
 * 思路:按斯涅尔定律折射;光线掠射角度越斜,反射越强(Schlick 近似)。
 * ref_idx = 折射率(玻璃约 1.5,空气约 1.0)。 */
class dielectric : public material
{
public:
    float ref_idx;
    __device__ dielectric(float ri) : ref_idx(ri) {}

    __device__ virtual bool scatter(const ray &r_in, const hit_record &rec,
                                    vec3 &attenuation, ray &scattered,
                                    curandState *local_rand_state) const
    {
        /* TODO 核心代码④:折射 + Schlick(见下方注释) */
        /* 1. 判断光线是从球外进球内,还是从球内出球外(点积法线和入射方向的正负)
         *    进:ni_over_nt = 1/ref_idx,出:ni_over_nt = ref_idx
         *    从内往外时还要把法线翻个面
         * 2. 尝试 refract;若发生全反射(返回 false)则改用 reflect
         * 3. 按 Schlick(cos, ref_idx) 的概率决定这次是反射还是折射
         * 4. attenuation = (1,1,1) 玻璃不吸颜色,返回 true */
        /* ============ 【批改】dielectric:全书最硬的一步,觉得吃力完全正常 ============
         * 先报喜:进出介质的判断(dot>0 → ni_over_nt=ref_idx,否则 1/ref_idx)是对的,和教科书一致。
         * 按严重程度排问题:
         * ✗ 1(当前唯一的编译错误,第 124 行):`= false` 是赋值不是比较!
         *    C 里判相等是 `==`,`=` 是"把右边塞进左边"。函数返回值是临时值,不能塞东西,
         *    所以报 "expression must be a modifiable lvalue"。改:`== false` 或更惯用 `!refract(...)`。
         * ✗ 2(致命逻辑,第 126 行):折射光线从来不会被装进 scattered!
         *    折射分支里只算了 cos 和 R,没有 scattered = ray(rec.p, refracted);
         *    玻璃折射出来的光线凭空消失了,画面里玻璃会缺一大块。
         * ✗ 3(缺半步,第 120-123 行):光线从玻璃里面出来(dot>0 分支)时,法线要翻面——
         *    折射/反射/Schlick 全都要用"朝内"的法线,即 rec.normal 取负。你现在出来那半
         *    永远拿朝外的法线算,数学错。
         * ⚠ 4(别扭但不致命):refract 被调了两次(124、126 行)。它是纯函数,两次结果其实一样,
         *    但既浪费又难读。标准写法:bool can_refract = refract(...); 只算一次,
         *    if (!can_refract) { 反射 } else { Schlick 二选一 }。
         * ⚠ 5(Schlick 的"概率"怎么用,你说的不会写的部分):R 本身就是"该反射的概率"!
         *    摇骰子:if (curand_uniform(local_rand_state) < R) → 走反射;否则 → 走折射。
         *    这就是把物理上的"越斜反射越强"变成随机二选一。
         * ⚠ 6(cos 细节):cos 要用单位向量、配合翻面后的法线算,保证是正数(参考 schlick 的批改)。
         * ⚠ 7(顺手):传给 refract/reflect 的入射方向要包 unit_vector,和 metal 同一个理由。
         * 批改结论:ni_over_nt 那步对,其余(翻法线→算一次折射→Schlick 二选一→装 scattered)
         * 需要重排。建议今晚只重写这一个函数,写完编译+跑图,别贪多。
         * =============================================================================== */
        vec3 unit_direction = unit_vector(r_in.direction()); /* 公式要求入射方向是单位向量,先归一化 */
        float ni_over_nt;
        vec3 outward_normal; /* 公式要求法线永远"迎着光来的那一边":进来时用表面法线,出去时翻面 */
        if (dot(unit_direction, rec.normal) > 0)/*如果与法线同向，也就是从内往外射 */
        {
            outward_normal = -rec.normal; // 出去:翻面!
            ni_over_nt = ref_idx;
        }
        else/*与法线反向 */
        {
            outward_normal = rec.normal;
            ni_over_nt = 1 / ref_idx;
        }
        vec3 refracted;//准备一个参数,这个是用来放下折射函数计算的折射方向的，到时候塞到scattered里面

        
        if (refract(unit_direction, outward_normal, ni_over_nt, refracted) == false)/*如果发生全反射 */
        {
            scattered = ray(rec.p, reflect(unit_direction, outward_normal));/*直接调用反射函数，塞进scattered完事，不发生折射 */
            attenuation = vec3(1, 1, 1); // 玻璃不吸颜色 —— 这条返回路径原来漏写了,不然颜色是乱码!
            return true;
        }
        else//折射成功了,refracted 盒子里已经装好折射方向,不用再调一次 refract
        {
            float cos = -dot(unit_direction, outward_normal);/*翻面后的法线迎着光,加负号保证 cos 是正数 */
            float R = schlick(cos, ref_idx);/* 算个概率:越斜反射率越高 */
            if (curand_uniform(local_rand_state) < R)/*摇骰子,摇中小于R就走反射 */
            {
                scattered = ray(rec.p, reflect(unit_direction, outward_normal));
            }
            else
            {
                scattered = ray(rec.p, refracted);/*折射方向在盒子里,端出来 */
            }
            attenuation = vec3(1, 1, 1);
            return true;
        }
    }
};

#endif
