// #region 0. 头文件(包含的库)
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#define NOMINMAX
#include <windows.h>
#include <windowsx.h> /* GET_X_LPARAM / GET_WHEEL_DELTA_WPARAM 这些鼠标消息拆包宏 */
#include <math.h>
#include "vec3.h"
#include "ray.h"
#include "hitable_list.h"
#include "sphere.h"
#include "bvh.h"
#include "camera.h"
#include <curand_kernel.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
using namespace std;
// #endregion 0

// #region 1. 文件说明(用法 + 操作手册,不用改)
/* =====================================================================
 * main_accum.cu —— M4 交互窗口:静止累计采样 + 双边滤波(时间 + 空间双降噪)
 *
 * 上一版(main_anim.cu)的噪点靠盒式模糊硬抹(拿清晰度换干净,创可贴);
 * 本版换成正统做法,两块拼起来正好是工业级去噪器(SVGF 等)的两大件:
 *   1. 静止累计采样(时间维):相机停下时,把同一机位的每一帧加进一个"累计账本",
 *      显示时除以已攒帧数 —— 这就是样本均值,你统计课里的东西:
 *      噪点幅度 ∝ 1/√光线数 → 攒 N 帧 = 每像素光线数 ×N → 噪点除以 √N,
 *      而且模糊丢掉的高频细节一根不少——账本只做加法,不抹任何东西。
 *   2. 双边滤波(空间维):有"判断力"的模糊——邻居要"离得近"(空间权重)
 *      且"颜色像"(值域权重)才有资格投票;同色区照常平滑,
 *      跨过边缘时对岸像素的票≈0,边缘保持锐利。
 * 组合拳:运动中 = 双边滤波单帧(空间维硬扛),暂停 = 攒 N 帧再双边抛光,
 *   时间维 + 空间维正交降噪,1+1>2。
 *
 * 操作(模仿 UE5 视口):
 *   右键拖动 = 旋转视角;中键拖动 = 平移(抓住世界拖);滚轮 = 前后缩放;
 *   F = 聚焦场景中心;按住右键 + WASD = 飞行模式(Q/E 升降);
 *   SPACE 暂停/恢复攒样本(标题栏显示 ACC xN),ESC 退出。
 * 相机:自由相机(位置 + 注视点两个点),不再绕圈;任何相机操作都会作废已攒的账本。
 *
 * 用法:
 *   accum.exe                → 720×360,4 spp + 双边滤波(sigma_r=0.15),一直播
 *   accum.exe 16 0 0         → 16 spp、不滤波(纯光线硬扛,暂停时攒得更快)
 *   accum.exe 4 3            → 播 3 秒自动退出(验收模式,控制台打印平均帧率)
 *   accum.exe 4 0 0.15 1280 720 → 1280×720 大图(4 spp 约 22 FPS,运动略卡)
 *   accum.exe 1 0 0.15 1280 720 → 1280×720 + 1 spp(约 87 FPS,暂停攒样本一样变干净)
 * 参数:[spp] [秒数,0=一直播] [sigma_r 颜色容忍度,0=关] [宽度] [高度]
 *   sigma_r 越大越敢抹(更干净,但细节越容易被抹掉),0.15 是保守默认,大胆试 0.3~0.5。
 * 窗口可以随便拖/双击标题栏最大化:画面保持宽高比、居中显示,多余部分留黑(信箱条),
 * 放大时用 HALFTONE 平滑插值,不会一格一格马赛克。
 * 分辨率账(实测 80M rays/s):720×360@4spp=77FPS;1280×720@4spp≈22 / @1spp≈87;
 *   1920×1080@4spp≈10(运动很卡,但用来"暂停攒一张干净的静态大图"完全没问题)。
 * 噪点账:噪点幅度 ∝ 1/√光线数 → 攒 N 帧噪点除以 √N;攒 100 帧 = 干净 10 倍。
 * 坑:ns 必须是完全平方数(1/4/9/16...),render 里 n = (int)sqrt(ns) 取整,
 *    传 2 只会画 1 根光线——main.cu 老规矩。
 * (本文件不动 main.cu / main_anim.cu / bvh.h,阶梯版本不受影响)
 * ===================================================================== */
// #endregion 1

// #region 2. GPU 渲染(光线追踪:render_init → color → render)
// ===== 以下核函数全部照抄 main.cu,只加一处:render 末尾写回随机数状态 =====
// (main.cu 只发射一次无所谓;这里每帧都发射,不写回的话每帧都从同一批骰子重画,
//   画面会"冻住"在原地闪,和甜点的坑是同一个)

__global__ void render_init(int max_x, int max_y, curandState *rand_state)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int j = threadIdx.y + blockIdx.y * blockDim.y;
    if ((i >= max_x) || (j >= max_y))
        return;
    int pixel_index = j * max_x + i;
    curand_init(1984 + pixel_index, 0, 0, &rand_state[pixel_index]);
}

__device__ vec3 color(const ray &r, hitable **world, int depth, curandState *local_rand_state)
{
    vec3 col(1.0f, 1.0f, 1.0f);
    ray cur_ray = r;
    for (int d = 0; d < depth; d++)
    {
        hit_record rec;
        if ((*world)->hit(cur_ray, 0.001f, 10000.f, rec))
        {
            ray scattered;
            vec3 attenuation;
            if (rec.mat_ptr->scatter(cur_ray, rec, attenuation, scattered, local_rand_state))
            {
                col *= attenuation;
                cur_ray = scattered;
            }
            else
                return vec3(0.0f, 0.0f, 0.0f);
        }
        else
        {
            vec3 unit_direction = unit_vector(cur_ray.direction());
            float t = 0.5f * (unit_direction.y() + 1.0f);
            return col * ((1.0f - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0));
        }
    }
    return vec3(0.0f, 0.0f, 0.0f);
}

__global__ void render(vec3 *fb, int max_x, int max_y, int ns, camera cam, hitable **world, curandState *rand_state)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int j = threadIdx.y + blockIdx.y * blockDim.y;
    if (i >= max_x || j >= max_y)
        return;
    int pixel_index = j * max_x + i;

    curandState local_rand_state = rand_state[pixel_index];
    vec3 col(0.0f, 0.0f, 0.0f);
    int n = (int)sqrt((float)ns);
    for (int si = 0; si < n; si++)
    {
        for (int sj = 0; sj < n; sj++)
        {
            float u = (i + (si + curand_uniform(&local_rand_state)) / n) / max_x;
            float v = (j + (sj + curand_uniform(&local_rand_state)) / n) / max_y;
            ray r = cam.get_ray(u, v, &local_rand_state);
            col += color(r, world, 50, &local_rand_state);
        }
    }
    fb[pixel_index] = col / float(ns);
    rand_state[pixel_index] = local_rand_state; /* 关键:这一帧的骰子状态存回去,下一帧接着走 */
}
// #endregion 2

// #region 3. GPU 降噪(双边滤波 + 累计账本)
/* 边缘保持去噪:双边滤波(今天在 lvbo.cu 练过的灰度版 → 升级成 RGB 版)
 * 每个邻居要拿两张票才有资格投票:
 *   空间权重 = gaussian(距离, sigma_s):离得近的票重;
 *   值域权重 = gaussian(颜色差, sigma_r):颜色像的票重,差太远的票≈0。
 * 于是同色区内照常平滑,跨过边缘时对岸的像素票≈0 → 边缘保持锐利。
 * sigma_r = 颜色容忍度(值域),调大 = 更敢抹;sigma_s 固定 2.0(半径 ceil(2σ)=4,9×9 窗口)。
 * RGB 版唯一的新东西:颜色差 = 把颜色当成三维空间的点,算两点距离
 *   sqrtf(dr² + dg² + db²),再喂给高斯。
 * 管线位置:渲染 → [暂停时] 账本累计 → 双边滤波(暂停滤账本,运动滤本帧)→ 上屏。 */

// TODO 双边①:高斯函数 —— 和 lvbo.cu 里你写的那行一模一样(expf 版)
__device__ float gaussian(float x, float sigma)
{
    return expf(-(x * x) / (2.0f * sigma * sigma));
}
// TODO 双边②:RGB 版双边滤波核函数
//   签名:__global__ void bilateral(const vec3 *input, vec3 *output,
//                                  int nx, int ny, float sigma_s, float sigma_r)
//   骨架照抄上面 render 的开头:i、j 从线程号算出来,越界 return,
//   pixel_index = j * nx + i;半径 radius = (int)ceilf(2.0f * sigma_s)。
//   中间照抄 lvbo.cu 的内层双循环,四处翻译:
//     - 邻居是 vec3 不是 float:邻居地址还是 ny * width + nx 那套,只是元素变大了
//     - 值域差 = RGB 空间距离:vec3 diff = input[pixel_index] - input[邻居];
//       float range_diff = sqrtf(diff.r()*diff.r() + diff.g()*diff.g() + diff.b()*diff.b());
//     - pixel_sum 是 vec3:pixel_sum += input[邻居] * total_weight(vec3 乘 float)
//     - 最后 output[pixel_index] = pixel_sum / weight_sum(除以权重和,灵魂别丢)
//   ⚠ 命名陷阱:本文件里 nx/ny 是图像尺寸,你的邻居坐标别叫 nx/ny(撞名会让
//   边界检查变成"邻居 < 邻居自己"恒假 → 所有邻居都越界 → 除零)。
//   邻居坐标叫 gx/gy 之类的别的名字。

__global__ void bilateral(const vec3 *input, vec3 *output, int nx, int ny, float sigma_s, float sigma_r)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int j = threadIdx.y + blockIdx.y * blockDim.y;
    if (i >= nx || j >= ny)
        return;
    int pixel_index = j * nx + i;
    int radius = (int)ceilf(2.0f * sigma_s);
    vec3 pixel_sum = vec3(0.0f, 0.0f, 0.0f); /* 记录加权之后的像素值之和 */
    float weight_sum = 0.0f;                 /* 权重总和 */
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            int gx = x + i;
            int gy = y + j;
            if (gx >= 0 && gx < nx && gy >= 0 && gy < ny)
            {
                int neibor_pixel = gy * nx + gx;                        /* 把半径内部的邻居坐标算出来 */
                float spatial_dist = sqrtf((float)(x * x + y * y));     /* 空间距离:窗口偏移量,和颜色无关 */
                vec3 diff = input[pixel_index] - input[neibor_pixel];   // 颜色差(值域距离)
                float spatial_weight = gaussian(spatial_dist, sigma_s); // 计算空间权重
                // 第二部分：计算值域权重 (Range Weight)
                /* 获取像素亮度差绝对值，并输入高斯函数 */
                float range_diff = sqrtf(diff.r() * diff.r() + diff.g() * diff.g() + diff.b() * diff.b());
                float range_weight = gaussian(range_diff, sigma_r);

                float total_weight = spatial_weight * range_weight;
                pixel_sum += input[neibor_pixel] * total_weight;
                weight_sum += total_weight;
            }
        }
    }
    output[pixel_index] = pixel_sum / weight_sum;
}
/* TODO 核心代码①:累计核函数 —— 暂停时每帧调用,把这一帧的画面加进账本
 * 数学就一行:fb_acc[pixel_index] += fb[pixel_index](账本 += 新帧)。
 * 骨架照抄 render 的开头:i、j 从线程号算出来,越界 return,
 * pixel_index = j * max_x + i,然后就是那行加法。
 * 签名:__global__ void accumulate(vec3 *fb_acc, const vec3 *fb, int max_x, int max_y)
 * (启动时用和 render 一模一样的 blocks/threads 2D 网格,索引方式照抄 render)
 */

__global__ void accumulative(vec3 *fb_acc, const vec3 *fb, int max_x, int max_y)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int j = threadIdx.y + blockIdx.y * blockDim.y;
    if (i >= max_x || j >= max_y)
        return;
    int pixel_index = j * max_x + i;
    fb_acc[pixel_index] += fb[pixel_index];
}
// #endregion 3
// #region 4. GPU 建场景(随机球世界:scene → create_world → free_world)
struct scene
{
    hitable **list;
    int capacity;
    int count;
    __device__ scene(hitable **l, int cap) : list(l), capacity(cap), count(0) {}
    __device__ void add(hitable *object)
    {
        if (count < capacity)
            list[count++] = object;
    }
};

__global__ void create_world(hitable **d_list, hitable **d_world, int capacity, int *d_num_objects)
{
    if (threadIdx.x == 0 && blockIdx.x == 0)
    {
        scene s(d_list, capacity);
        curandState st;
        curand_init(1234, 0, 0, &st); /* 场景种子:和 main.cu 一致,场景布局逐字节相同 */
        s.add(new sphere(vec3(0, -1000, 0), 1000, new lambertian(vec3(0.5, 0.5, 0.5))));
        s.add(new sphere(vec3(0, 1, 0), 1, new dielectric(1.5f)));
        s.add(new sphere(vec3(-4, 1, 0), 1, new lambertian(vec3(0.4, 0.2, 0.1))));
        s.add(new sphere(vec3(4, 1, 0), 1, new metal(vec3(0.7, 0.6, 0.5), 0)));
        for (int a = -11; a <= 10; a++)
        {
            for (int b = -11; b <= 10; b++)
            {
                auto choose_mat = curand_uniform(&st);
                vec3 center = vec3(a + 0.9 * curand_uniform(&st), 0.2, b + 0.9 * curand_uniform(&st));
                if ((center - vec3(4, 0.2, 0)).length() < 0.9)
                    continue;
                else
                {
                    if (choose_mat < 0.8)
                    {
                        auto albedo = vec3(curand_uniform(&st), curand_uniform(&st), curand_uniform(&st)) * vec3(curand_uniform(&st), curand_uniform(&st), curand_uniform(&st));
                        s.add(new sphere(center, 0.2, new lambertian(albedo)));
                    }
                    else if (choose_mat < 0.95)
                    {
                        auto albedo = vec3(0.5f + 0.5f * curand_uniform(&st), 0.5f + 0.5f * curand_uniform(&st), 0.5f + 0.5f * curand_uniform(&st));
                        auto fuzz = 0.5f * curand_uniform(&st);
                        s.add(new sphere(center, 0.2, new metal(albedo, fuzz)));
                    }
                    else
                    {
                        s.add(new sphere(center, 0.2, new dielectric(1.5f)));
                    }
                }
            }
        }
        *d_world = new bvh_node(d_list, 0, s.count);
        *d_num_objects = s.count;
    }
}

__global__ void free_world(hitable **d_list, hitable **d_world, int num_objects)
{
    if (threadIdx.x == 0 && blockIdx.x == 0)
    {
        ((bvh_node *)(*d_world))->destroy();
    }
}
// #endregion 4

// #region 5. 全局账本(相机两个点 + 鼠标键盘状态)
// ===== 自由相机(UE5 视口操作)=====
// 相机状态 = 两个点:位置 + 注视点,主循环每帧用它俩构造 camera。
// 旋转只绕位置转注视点(距离不变);平移/缩放/飞行都是两点一起挪。
// 于是注视点 = 旋转的圆心 + 缩放的靶心 + 移动的方向基准,一个点干三份活(UE5 的 orbit pivot 同理)。
// SPACE 切换暂停。注意用 WM_KEYUP 而不是 WM_KEYDOWN:按住不放时 Windows 会连发
// WM_KEYDOWN(按键自动重复),SPACE 会被来回翻转好几十次;WM_KEYUP 只在你松手时来一次。
// (ESC 用 KEYDOWN 无所谓,退出两次还是退出)
static int g_paused = 0;                    // 1 = 开始攒样本(SPACE 切换)
static vec3 g_cam_pos(13.0f, 2.0f, 3.0f);   /* 相机位置,起点 = 毕业机位 */
static vec3 g_cam_target(0.0f, 0.0f, 0.0f); /* 注视点,初始看向场景中心 */
static int g_cam_dirty = 0;                 // 相机被用户动过 → 已攒的账本作废(明天"手停即攒"的雏形)

/* 鼠标/键盘状态:窗口消息里记录,消息处理或主循环里消费 */
static int g_rmb_down = 0, g_mmb_down = 0;                                               /* 右键/中键是否按住 */
static int g_last_mx = 0, g_last_my = 0;                                                 // 上一次鼠标位置(拖动起点)
static int g_key_w = 0, g_key_a = 0, g_key_s = 0, g_key_d = 0, g_key_q = 0, g_key_e = 0; /* 飞行键 */

static const float PI = 3.14159265f;

/* 相机碰撞体:GPU 场景的"大球目录"(CPU 侧副本,只抄 4 个大球,和 create_world 逐字对齐)
 * 守门员 resolve_collision 拿它把相机挡在球外:视点不会沉进地面,也不会怼进大球。
 * 为什么只抄大球:小球本来就该离远看,全挡反而在球堆里卡得心烦(UE5 也只挡大几何)。 */
struct collision_sphere
{
    vec3 center;
    float radius;
};
static const collision_sphere g_colliders[] = {
    { vec3(0.0f, -1000.0f, 0.0f), 1000.0f }, /* 地面大球(顶面 y=0) */
    { vec3(0.0f, 1.0f, 0.0f), 1.0f },        /* 玻璃球 */
    { vec3(-4.0f, 1.0f, 0.0f), 1.0f },       /* 漫反射球 */
    { vec3(4.0f, 1.0f, 0.0f), 1.0f },        /* 金属球 */
};

/* 守门员:所有写回 g_cam_pos 的操作必须过这一关 —— 钻进大球就沿径向推出来(留 0.2 余量)
 * 径向推挤只修正"沉进去"的分量,贴地滑行的水平分量不受影响 → 贴地走 = 自然滑行 */
static vec3 resolve_collision(const vec3 &pos)
{
    vec3 p = pos;
    int n = sizeof(g_colliders) / sizeof(g_colliders[0]);
    for (int i = 0; i < n; i++)
    {
        vec3 to_center = p - g_colliders[i].center;
        float d = to_center.length();
        float limit = g_colliders[i].radius + 0.2f;
        if (d < limit)
        {
            if (d < 1e-6f)
                p = g_colliders[i].center + vec3(0.0f, limit, 0.0f); /* 恰在球心:往头顶推,避免除零 */
            else
                p = g_colliders[i].center + to_center * (limit / d);
        }
    }
    return p;
}
// #endregion 5

// #region 6. 相机数学(旋转/平移/缩放/聚焦/飞行 —— 你写的 A 组)
// TODO 相机①:基向量函数 —— 从"位置+注视点"算右-上-前三个基向量(右手系)
//   fwd   = unit_vector(target - pos)
//   right = unit_vector(cross(fwd, vec3(0, 1, 0)))   ← 世界向上 (0,1,0)
//   up    = cross(right, fwd)                        ← 自动就是单位向量,不用再归一
//   (vec3.h 自带 cross() 和 unit_vector(),去翻一眼)
static void cam_basis(const vec3 &pos, const vec3 &target, vec3 &fwd, vec3 &right, vec3 &up)
{
    fwd = unit_vector(target - pos);
    right = unit_vector(cross(fwd, vec3(0, 1, 0)));
    up = cross(right, fwd);
}

// TODO 相机②:Rodrigues 绕轴旋转公式(轴 k 必须已单位化):
//   v' = v*cos(a) + cross(k, v)*sin(a) + k * dot(k, v) * (1 - cos(a))
//   直观理解:把 v 拆成"平行 k 的一份"(转不动,原样保留)+ "垂直 k 的一份"
//   (在垂直平面里转 a 角;cross(k,v) 就是它转过 90° 后的方向)。
/*   这一把下去,后面旋转/俯仰全靠它。 */
static vec3 rotate_around(const vec3 &v, const vec3 &k, float angle)
{
    vec3 k_1 = unit_vector(k);
    vec3 v_1 = v * cos(angle) + cross(k_1, v) * sin(angle) + k_1 * dot(k_1, v) * (1 - cos(angle));
    return v_1; /* 换成公式 */
}

// TODO 相机③:右键旋转(UE5 转视角)。
//   1) 先拿基向量:cam_basis(pos, target, fwd, right, up);dist = (target-pos).length();
//   2) yaw(左右转):fwd = rotate_around(fwd, vec3(0,1,0), -dx * 0.002f);
//      (左右 0.002 / 俯仰 0.0015 = 弧度/像素,手感可调)
//   3) pitch(俯仰):cur = asinf(fwd.y());   ← fwd 已单位化,俯仰角 = fwd 的 y 分量反正弦
//      new = clamp(cur - dy * 0.0015f, -MAX_PITCH, MAX_PITCH),MAX_PITCH = 89.0f * PI / 180.0f;
//      fwd = rotate_around(fwd, right, new - cur);   ← 转的是"差值",钳位才稳
//      (钳到 89°:正到 90° 时 fwd 和世界向上平行,cross(fwd, up) 变成零向量,相机会翻个底朝天)
//   4) target = pos + fwd * dist(距离不变 = 只转不推拉);
//   5) g_cam_dirty = 1;
/*   符号检查:右拖 → 画面往右转;下拖 → 低头。反了就把对应 dx/dy 前的符号翻过来。 */
static void cam_look(float dx_1, float dy_1)
{
    float dx= -dx_1;
    float dy=-dy_1;
    vec3 pos = g_cam_pos, target = g_cam_target, fwd, right, up; /* 第一步:从全局账本读相机状态 */
    cam_basis(pos, target, fwd, right, up);
    float dist = (target - pos).length();
    /* 左右转 */
    fwd = rotate_around(fwd, vec3(0, 1, 0), -dx * 0.002f);

    float cur = asinf(fwd.y());
    float MAX_PITCH = 89.0f * PI / 180.0f;
    float new_1 = cur - dy * 0.0015f;
    if (new_1 > MAX_PITCH)
        new_1 = MAX_PITCH; // 钳位 = 两个 if 夹住(见讲解)
    if (new_1 < -MAX_PITCH)
        new_1 = -MAX_PITCH;
    fwd = rotate_around(fwd, right, new_1 - cur);
    g_cam_target = pos + fwd * dist; // 最后一步:算完写回全局账本(旋转只动注视点)
    g_cam_dirty = 1;
}

// TODO 相机④:中键平移(UE5"抓住世界拖动":往哪拖,场景跟哪走 → 相机反向走)。
//   offset = (right * (-dx) + up * dy) * 0.001f * dist   ← 乘 dist:离得近拖得少,
//     不乘的话近距离拖动会像拿显微镜,手一抖画面飞出银河系
//   pos += offset;target += offset;g_cam_dirty = 1;
static void cam_pan(float dx, float dy)
{
    vec3 pos = g_cam_pos, target = g_cam_target, fwd, right, up; /* 第一步:从全局账本读相机状态 */
    cam_basis(pos, target, fwd, right, up);
    float dist = (target - pos).length();
    auto offset = (right * (-dx) + up * dy) * 0.001f * dist;
    pos += offset;
    target += offset;
    g_cam_pos = resolve_collision(pos); /* 最后一步:写回账本(写回前先过守门员,防穿模) */
    g_cam_target = target;
    g_cam_dirty = 1;
}

// TODO 相机⑤:滚轮 dolly。wheel > 0(上滚)= 凑近目标。
//   notch = wheel / 120.0f(一格 = 120 的倍数);
//   offset = fwd * notch * 0.1f * dist(每格 10%);
//   pos += offset;target += offset;
//   钳位:新 dist < 0.5 → pos = target - fwd * 0.5(别穿过注视点,穿过去画面会翻);
//   g_cam_dirty = 1;
static void cam_dolly(int wheel)
{
    vec3 pos = g_cam_pos, target = g_cam_target, fwd, right, up; /* 第一步:从全局账本读相机状态 */
    cam_basis(pos, target, fwd, right, up);
    float dist = (target - pos).length();

    float notch = wheel / 120.f;
    auto offset = fwd * notch * 0.1f * dist;
    pos += offset;
    target += offset;
    if ((target - pos).length() < 0.5f)
        pos = target - fwd * 0.5f; // 补:保险丝——离注视点太近就拉回 0.5,别穿过它(穿过去视线反转)
    g_cam_pos = resolve_collision(pos); /* 补:最后一步写回账本(写回前先过守门员,防穿模) */
    g_cam_target = target;
    g_cam_dirty = 1;
}

// TODO 相机⑥:F 聚焦。没有选择系统 → 聚焦场景中心:
//   g_cam_target = vec3(0, 0, 0);(位置不动,视线瞬间转过去)
//   g_cam_dirty = 1;
static void cam_focus()
{
    g_cam_target = vec3(0, 0, 0);
    g_cam_dirty = 1;
}

// TODO 相机⑦:飞行模式(按住右键 + WASD 移动,Q/E 升降)。speed = 世界单位/秒。
//   先 cam_basis 拿三兄弟,然后:
//   offset = (right * (g_key_d - g_key_a)
//           + up    * (g_key_e - g_key_q)   ← 沿相机 up(低头后 Q/E 仍贴视角)
//           + fwd   * (g_key_w - g_key_s)) * 2.0f * dt;
//   pos += offset;target += offset;g_cam_dirty = 1;
//   (按键是"状态"不是"事件":按住 W 就持续飞;由主循环每帧调用,dt 那边有)
static void cam_fly(float dt)
{
    vec3 pos = g_cam_pos, target = g_cam_target, fwd, right, up; /* 第一步:从全局账本读相机状态 */
    cam_basis(pos, target, fwd, right, up);
    float dist = (target - pos).length();

    auto offset = (right * (g_key_d - g_key_a) + up * (g_key_e - g_key_q) + fwd * (g_key_w - g_key_s)) * 2.0f * dt;
    pos += offset;
    target += offset;
    g_cam_pos = resolve_collision(pos); /* 补:最后一步写回账本(写回前先过守门员,防穿模) */
    g_cam_target = target;
    g_cam_dirty = 1;
}
// #endregion 6

// #region 7. 窗口消息(把鼠标/键盘翻译成状态 —— B 组)
/* 窗口消息:ESC 退出,SPACE 切换暂停,鼠标/键盘驱动自由相机 */
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN && w == VK_ESCAPE)
    {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_KEYUP && w == VK_SPACE)
    {
        g_paused = !g_paused;
        return 0;
    }
    // 右键:按下 = 开始拖。记下起点 + SetCapture 抓牢鼠标(拖出窗口外也继续收 MOUSEMOVE;
    // 抬起时 ReleaseCapture 归还鼠标)
    if (msg == WM_RBUTTONDOWN)
    {
        g_rmb_down = 1;
        g_last_mx = GET_X_LPARAM(l);
        g_last_my = GET_Y_LPARAM(l);
        SetCapture(hwnd);
        return 0;
    }
    if (msg == WM_RBUTTONUP)
    {
        g_rmb_down = 0;
        ReleaseCapture();
        return 0;
    }
    // TODO 消息②:中键按下/抬起 —— 和右键一模一样,只是换成 g_mmb_down
    if (msg == WM_MBUTTONDOWN) /* 按下 */
    {
        g_mmb_down = 1; /* 记录中键正按着 */
        g_last_mx = GET_X_LPARAM(l); // 参数是小写字母 l,不是数字 1!
        g_last_my = GET_Y_LPARAM(l);
        SetCapture(hwnd);
        return 0;
        /* 你写(参考右键) */
    }
    if (msg == WM_MBUTTONUP) /* 抬起 */
    {
        g_mmb_down = 0; // 中键状态清零(照抄右键时别忘了换变量名!)
        ReleaseCapture();
        return 0;
    }
    // TODO 消息③:拖动 —— 新位置减上次位置 = 增量,按"谁按住"派活,最后刷新 g_last_mx/my:
    //     int dx = mx - g_last_mx, dy = my - g_last_my;
    //     if (g_rmb_down) cam_look((float)dx, (float)dy);
    //     else if (g_mmb_down) cam_pan((float)dx, (float)dy);
    //     g_last_mx = mx;g_last_my = my;
    /*   ⚠ 最后两行别忘:忘了 = 增量无限累加,右键一按画面原地电风扇 */
    if (msg == WM_MOUSEMOVE)
    {
        int mx = GET_X_LPARAM(l);
        int my = GET_Y_LPARAM(l);
        int dx = mx - g_last_mx;
        int dy = my - g_last_my;
        if (g_rmb_down)
            cam_look(dx, dy);
        else if (g_mmb_down)
            cam_pan(dx, dy);
        g_last_mx = mx;
        g_last_my = my;
        return 0;
    }
    // TODO 消息④:滚轮 —— GET_WHEEL_DELTA_WPARAM(w) 交给 cam_dolly(上滚 = 正)
    if (msg == WM_MOUSEWHEEL)
    {
        /* 你写 */
        int wheel_1 = GET_WHEEL_DELTA_WPARAM(w);
        cam_dolly(wheel_1);
        return 0;
    }
    if (msg == WM_KEYDOWN && w == 'W')
    {
        g_key_w = 1;
        return 0;
    }
    if (msg == WM_KEYUP && w == 'W')
    {
        g_key_w = 0;
        return 0;
    }
    if (msg == WM_KEYDOWN && w == 'A')
    {
        g_key_a = 1;
        return 0;
    }
    if (msg == WM_KEYUP && w == 'A')
    {
        g_key_a = 0;
        return 0;
    }
    if (msg == WM_KEYDOWN && w == 'S')
    {
        g_key_s = 1;
        return 0;
    }
    if (msg == WM_KEYUP && w == 'S')
    {
        g_key_s = 0;
        return 0;
    }
    if (msg == WM_KEYDOWN && w == 'D')
    {
        g_key_d = 1;
        return 0;
    }
    if (msg == WM_KEYUP && w == 'D')
    {
        g_key_d = 0;
        return 0;
    }
    if (msg == WM_KEYDOWN && w == 'Q')
    {
        g_key_q = 1;
        return 0;
    }
    if (msg == WM_KEYUP && w == 'Q')
    {
        g_key_q = 0;
        return 0;
    }
    if (msg == WM_KEYDOWN && w == 'E')
    {
        g_key_e = 1;
        return 0;
    }
    if (msg == WM_KEYUP && w == 'E')
    {
        g_key_e = 0;
        return 0;
    }
    if (msg == WM_KEYDOWN && w == 'F')
    {
        cam_focus();
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1; // 背景我们自己画,别让系统擦——它擦一下,你就看到闪一下(return 1 = "已处理,别擦")
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}
// #endregion 7

// #region 8. main(总指挥:开局 → 窗口 → 循环 → 收摊)
int main(int argc, char **argv)
{
    // #region 8a. 参数解析 + GPU 开局(分配显存 / 建场景)
    int nx = (argc > 4) ? atoi(argv[4]) : 720; // 渲染分辨率(默认 720×360 = 60Hz 预算;2K 屏想要大图传 [宽度] [高度])
    int ny = (argc > 5) ? atoi(argv[5]) : 360;
    int ns = (argc > 1) ? atoi(argv[1]) : 4;                      // 每帧采样数(必须完全平方数)
    float max_seconds = (argc > 2) ? (float)atof(argv[2]) : 0.0f; /* 0 = 一直播 */
    float sigma_r = (argc > 3) ? (float)atof(argv[3]) : 0.15f;    /* 双边滤波:颜色容忍度,0 = 关 */
    const float sigma_s = 2.0f;                                   // 空间权重的高斯 σ(窗口半径 = ceil(2σ) = 4,即 9×9)

    // ===== GPU 侧:和 main.cu 一模一样的开局 =====
    cudaDeviceSetLimit(cudaLimitStackSize, 32768);
    int num_pixels = nx * ny;
    size_t fb_size = num_pixels * sizeof(vec3);
    vec3 *fb;
    cudaMalloc((void **)&fb, fb_size); // 显存帧缓冲(不再用统一内存:动画每帧要读,页迁移税 34ms/帧,见诊断)
    vec3 *fb_host;
    cudaMallocHost((void **)&fb_host, fb_size); /* 钉住(pinned)的主机暂存区:每帧一次 memcpy,读它没有页故障 */
    vec3 *fb_bi;
    cudaMalloc((void **)&fb_bi, fb_size); // 双边滤波后的帧缓冲(bilateral 核函数的输出)
    vec3 *fb_acc;
    cudaMalloc((void **)&fb_acc, fb_size); /* 累计账本:暂停时把每帧画面加进来,显示时除以已攒帧数 */

    curandState *d_rand_state;
    cudaMalloc((void **)&d_rand_state, num_pixels * sizeof(curandState));

    const int MAX_OBJECTS = 1024;
    hitable **d_list;
    hitable **d_world;
    int *d_num_objects;
    cudaMalloc((void **)&d_list, MAX_OBJECTS * sizeof(hitable *));
    cudaMalloc((void **)&d_world, sizeof(hitable *));
    cudaMalloc((void **)&d_num_objects, sizeof(int));
    create_world<<<1, 1>>>(d_list, d_world, MAX_OBJECTS, d_num_objects);

    dim3 threads(8, 8);
    dim3 blocks((nx + threads.x - 1) / threads.x, (ny + threads.y - 1) / threads.y);
    render_init<<<blocks, threads>>>(nx, ny, d_rand_state);
    cudaDeviceSynchronize();

    /* 暂停状态机:g_paused 由窗口消息翻转,主循环每帧看它一眼 */
    bool paused = false;
    int accum_count = 0;       // 这个暂停会话里已累计了几帧(0 = 没在攒,显示用原图)
    float last_frame_t = 0.0f; // 上一帧的时刻(飞行模式算 dt 用)
    // #endregion 8a

    // #region 8b. 开窗口(GDI:注册 / 创建 / DIB 画布)
    // ===== 开窗口(GDI,Windows 自带,不用装任何库)=====
    SetProcessDPIAware();
    WNDCLASSA wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "CudaAccumWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    RECT rc = {0, 0, nx, ny};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "CUDA Ray Tracer",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                NULL, NULL, wc.hInstance, NULL);
    if (!hwnd)
    {
        cerr << "Failed to create window" << endl;
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);

    /* 32 位 DIB(负高度 = 从上往下),每帧把 fb 转成 BGRA 写进这块内存,再 StretchDIBits 上屏 */
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = nx;
    bmi.bmiHeader.biHeight = -ny;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *dib_bits = NULL;
    HBITMAP dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dib_bits, NULL, 0);
    ReleaseDC(hwnd, hdc);
    // #endregion 8b

    // #region 8c. 主循环(抽消息 → 账本作废 → dt/飞行 → 渲染 → 累计 → 降噪 → 上屏)
    // ===== 主循环:渲染一帧 → 转颜色 → 上屏,同时不停地抽窗口消息 =====
    MSG msg;
    int quit = 0;
    int frames = 0;
    float last_report = 0.0f;
    double sec_render = 0.0, sec_convert = 0.0, sec_blit = 0.0; // 每段耗时累计(诊断用)
    int last_cw = -1, last_ch = -1;                             // 上一次的窗口客户区尺寸(变了才刷黑,防闪)
    auto t_start = chrono::steady_clock::now();
    while (!quit)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                quit = 1;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (quit)
            break;

        auto now = chrono::steady_clock::now();
        float t = chrono::duration<float>(now - t_start).count();
        if (max_seconds > 0.0f && t >= max_seconds)
            break;

        // 相机一动,攒过的账本全部作废——账本里的样本全是旧视角的,混进新视角 = 重影。
        // 必须放在暂停状态机之前:先作废,下面该开新账的开新账、该接着攒的接着攒。
        // TODO 核心代码②改(相机动 → 账本作废):
        //     if (g_cam_dirty && accum_count > 0) {
        //         cudaMemset(fb_acc, 0, fb_size);
        //         accum_count = 0;
        //     }
        //     g_cam_dirty = 0;
        // (写完把这两段注释换成你的代码)
        if (g_cam_dirty && accum_count > 0)
        {
            cudaMemset(fb_acc, 0, fb_size);
            accum_count = 0;
        }
        g_cam_dirty = 0;
        // 暂停状态机:只有"刚按下 SPACE"的那一帧才开新账(你的老代码,原样保留)
        if (g_paused != 0 && !paused) // 只有"刚按下 SPACE"的那一帧才开新账:
        {
            cudaMemset(fb_acc, 0, fb_size); // 新账本清零(上一个会话攒的作废)
            accum_count = 0;
        }
        paused = (g_paused != 0); // 按下空格之后变成一

        // 相机不再绕圈:位置/注视点被鼠标键盘直接改(g_cam_pos/g_cam_target),
        // 主循环只负责每帧读它们构造相机。
        // TODO 核心代码③:飞行模式(按住右键 + WASD/QE;先算 dt 再飞,速度不随帧率漂):
        //     float dt = t - last_frame_t;last_frame_t = t;
        //     if (g_rmb_down) cam_fly(dt);
        /* ⚠ 必须放在构造 camera 之前:fly 动了 g_cam_pos,相机要吃到新位置 */
        float dt = t - last_frame_t;
        last_frame_t = t;
        if (g_rmb_down)
            cam_fly(dt);
        camera cam(g_cam_pos, g_cam_target, vec3(0, 1, 0),
                   20.0f, float(nx) / float(ny), 0.1f, 10.0f);
        auto t1 = chrono::steady_clock::now();
        render<<<blocks, threads>>>(fb, nx, ny, ns, cam, d_world, d_rand_state);
        if (!paused && accum_count > 0)
            accum_count = 0; // 恢复移动:旧账本作废(标题 ACC 归零,显示回到实时画面)
        // TODO 核心代码③a:暂停时把这帧加进账本(accum_count 记录已攒几帧)
        //     if (paused) {
        //         accumulate<<<blocks, threads>>>(fb_acc, fb, nx, ny);
        //         accum_count++;
        //     }
        // (accumulate 用和 render 一样的 blocks/threads 网格,i/j 索引照抄 render)
        if (paused)
        {
            accumulative<<<blocks, threads>>>(fb_acc, fb, nx, ny);
            accum_count++;
        }
        // 双边滤波:暂停滤账本,运动滤本帧。
        // 账本是 N 帧之和,数值放大 N 倍 → sigma_r 也放大 N 倍,才等效于"先除以 N 再滤"
        // (颜色差和 sigma_r 同乘 N,高斯权重逐像素不变 → 完全等效,不必真的先除)
        vec3 *bil_src = paused ? fb_acc : fb;
        // TODO 双边③:发射(队形照抄 render 的 blocks/threads 大军)
        //     if (sigma_r > 0.0f)   // sigma_r=0 时别发射:高斯里 2*σ*σ 除零,输出全 NaN
        //         bilateral<<<blocks, threads>>>(bil_src, fb_bi, nx, ny, sigma_s,
        //                                         sigma_r * (paused ? (float)accum_count : 1.0f));
        //     ⚠ 顺序:先输入后输出,和签名 (input, output, ...) 对齐!
        //     (写反编译器不报错——两个都是 vec3*,类型一样,只能靠约定和眼睛)
        if (sigma_r > 0.0f)
            bilateral<<<blocks, threads>>>(bil_src, fb_bi, nx, ny, sigma_s, sigma_r * (paused ? (float)accum_count : 1.0f));
        cudaMemcpy(fb_host, sigma_r > 0.0f ? fb_bi : bil_src,
                   fb_size, cudaMemcpyDeviceToHost); // 顺带完成了等待(同一条流,核函数跑完才轮到它)
        auto t2 = chrono::steady_clock::now();
        sec_render += chrono::duration<double>(t2 - t1).count();

        // fb_host(线性 RGB float)→ DIB(BGRA 8bit,gamma 开根,和 main.cu 输出同款)
        // 行序要对齐:fb 第 0 行 = 图像最下面一行(main.cu 写 PPM 时从 j=ny-1 往下数,顶行在前);
        // DIB 第 0 行 = 屏幕最上面一行(负高度 = 从上往下)。倒着取,画面才不倒。
        // 样本均值:账本总和 ÷ 已攒帧数(inv_count 上面已备好;不攒时 = 1,等于不除)
        float inv_count = (accum_count > 0) ? 1.0f / float(accum_count) : 1.0f;
        unsigned char *p = (unsigned char *)dib_bits;
        for (int r = 0; r < ny; r++)
        {
            int src_row = ny - 1 - r; /* 屏幕第 r 行 = fb 的第 (ny-1-r) 行 */
            for (int c = 0; c < nx; c++)
            {
                /* TODO 核心代码③b:样本均值除法(一行)——把账本总和除以已攒帧数 */
                const vec3 &col = fb_host[src_row * nx + c] * inv_count;
                p[0] = (unsigned char)(255.99f * sqrtf(col.b()));
                p[1] = (unsigned char)(255.99f * sqrtf(col.g()));
                p[2] = (unsigned char)(255.99f * sqrtf(col.r()));
                p[3] = 255;
                p += 4;
            }
        }

        auto t3 = chrono::steady_clock::now();
        sec_convert += chrono::duration<double>(t3 - t2).count();
        hdc = GetDC(hwnd);
        RECT client;
        GetClientRect(hwnd, &client);
        // 保持宽高比:目标区域 = 客户区内能容纳的最大 nx:ny 矩形,居中,四周留黑(信箱条)
        int cw = client.right - client.left;
        int ch = client.bottom - client.top;
        int dw = cw, dh = (int)((long long)cw * ny / nx);
        if (dh > ch)
        {
            dh = ch;
            dw = (int)((long long)ch * nx / ny);
        }
        int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
        if (cw != last_cw || ch != last_ch)
        {
            last_cw = cw;
            last_ch = ch;
            PatBlt(hdc, 0, 0, cw, ch, BLACKNESS); /* 尺寸变了才整面刷一次黑(信箱条);每帧刷 = 黑闪 */
        }
        SetStretchBltMode(hdc, HALFTONE); // 平滑放大(默认 COLORONCOLOR 是最近邻,放大全是马赛克)
        StretchDIBits(hdc, dx, dy, dw, dh,
                      0, 0, nx, ny, dib_bits, &bmi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(hwnd, hdc);
        auto t4 = chrono::steady_clock::now();
        sec_blit += chrono::duration<double>(t4 - t3).count();
        frames++;

        if (t - last_report >= 1.0f)
        {
            last_report = t;
            float fps = frames / t;
            char title[128];
            sprintf(title, "CUDA Ray Tracer - %.1f FPS - %d spp - ACC x%d - RMB rotate MMB pan wheel zoom F focus RMB+WASD fly - SPACE pause - ESC quit",
                    fps, ns, accum_count);
            SetWindowTextA(hwnd, title);
            printf("t=%.1fs  frames=%d  avg %.1f FPS  accum=%d\n", t, frames, fps, accum_count);
            fflush(stdout);
        }
    }
    // #endregion 8c

    // #region 8d. 结算(打印帧率)+ 收摊(释放显存)
    float t = chrono::duration<float>(chrono::steady_clock::now() - t_start).count();
    if (t < 1e-3f)
        t = 1e-3f;
    float fps = frames / t;
    float rays = (float)frames * num_pixels * ns;
    printf("Done: %d frames / %.1fs -> avg %.1f FPS -> %.2f M rays/s\n",
           frames, t, fps, rays / t / 1000000.0f);
    printf("breakdown per frame: render+sync %.1fms  convert %.1fms  blit %.1fms  other %.1fms\n",
           sec_render * 1000.0 / frames, sec_convert * 1000.0 / frames,
           sec_blit * 1000.0 / frames, (t - sec_render - sec_convert - sec_blit) * 1000.0 / frames);
    fflush(stdout);

    // ===== 收摊(和 main.cu 一样)=====
    int num_objects = 0;
    cudaMemcpy(&num_objects, d_num_objects, sizeof(int), cudaMemcpyDeviceToHost);
    free_world<<<1, 1>>>(d_list, d_world, num_objects);
    cudaFree(d_num_objects);
    cudaFree(d_list);
    cudaFree(d_world);
    cudaFree(d_rand_state);
    cudaFree(fb);
    cudaFree(fb_bi);
    cudaFree(fb_acc);
    cudaFreeHost(fb_host);
    DeleteObject(dib);
    DestroyWindow(hwnd);
    // #endregion 8d
    // #endregion 8
    return 0;
}
