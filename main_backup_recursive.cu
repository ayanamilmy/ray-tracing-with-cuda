#include <iostream>
#include <math.h>
#include "vec3.h"
#include "ray.h"
#include "hitable_list.h"
#include "sphere.h"
#include "bvh.h"
#include "camera.h"
#include <curand_kernel.h>
using namespace std;

/*以下是需要知道的前置知识
1.照片是分成宽和高的，宽是横着的，高是竖着的
2.要给宽和高分别分线程
3.颜色是通过一个vec3向量实现的，装载到vec3类fd数组中
*/

// 这个是个每个像素分配独立随机种子的核函数
__global__ void render_init(int max_x, int max_y, curandState *rand_state)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int j = threadIdx.y + blockIdx.y * blockDim.y;
    if ((i >= max_x) || (j >= max_y))
        return;

    int pixel_index = j * max_x + i;
    // 极其精妙的一行：用 1984 当基数，加上每个像素自己的索引，确保这几万个骰子摇出来的轨迹完全不同
    curand_init(1984 + pixel_index, 0, 0, &rand_state[pixel_index]);
}

// 在GPU上运行的颜色计算函数，就是用来算背景那个蓝色天空的,如果打中东西了就可以算任何颜色，这个应该是最核心的
__device__ vec3 color(const ray &r, hitable **world, int depth, curandState *local_rand_state) // depth 用完了(≤ 0)返回黑色,防止光线无限弹下去。
{
    if (depth <= 0)
        return vec3(0.0f, 0.0f, 0.0f);
    hit_record rec;                              // 准备一个空的结构体实例，用来保存一下打中瞬间的情况
    if ((*world)->hit(r, 0.001f, 10000.0f, rec)) // world是一个指针的指针，指向的是被击中的物体列表。*world对其进行解引用，也就是实例，调用一下这个类的hit函数。hit函数是bool类型的，作用是看你有没有击中任何东西
    {
        ray scattered;
        vec3 attenuation;
        if (rec.mat_ptr->scatter(r, rec, attenuation, scattered, local_rand_state))    // mat_ptr就是结构体名字，scatter是布尔类型的
            return attenuation * color(scattered, world, depth - 1, local_rand_state); // 进行递归，多弹一次
        return vec3(0, 0, 0);                                                          // 没弹出去 → 这条光死了,贡献黑色
    }
    else
    {
        vec3 unit_direction = unit_vector(r.direction()); // 把光线的方向向量归一化（长度变成1）
        // 把 y 轴的值从 [-1.0, 1.0] 映射到 [0.0, 1.0]
        float t = 0.5f * (unit_direction.y() + 1.0f);
        // 线性插值（Lerp）：根据 t 的值在白色和浅蓝色之间混合
        return (1.0f - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
    }
}

// 这个是把算出来的颜色塞进数组的，用GPU来做，这个才是最核心的
__global__ void render(vec3 *fb, int max_x, int max_y, int ns, camera cam, hitable **world, curandState *rand_state)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int j = threadIdx.y + blockIdx.y * blockDim.y;
    if (i >= max_x || j >= max_y)
        return;
    int pixel_index = j * max_x + i; // 第(j,i)个像素索引，max_x是照片的宽（横着的），已经过了j行了，每一行的宽度是max_x

    curandState local_rand_state = rand_state[pixel_index];
    vec3 col(0.0f, 0.0f, 0.0f); // 攒钱罐:这个像素的 ns 根光线都往这里累加
    /* TODO 核心代码⑪:分层采样(把"全像素随机瞎抖"升级成"10×10 小格每格必射一根")
     * 1. int n = (int)sqrt((float)ns);   —— 100 根光线 = 10×10 小格,n 是每边的格数
     * 2. 双层循环 si、sj 都从 0 到 n-1,每格射一根:
     *    float u = (i + (si + curand_uniform(&local_rand_state)) / n) / max_x;
     *      —— 读法:第 si 格的左上角是 si,加格内随机偏移(0~1),除以 n 缩进像素里,再整体除以 max_x
     *    float v = (j + (sj + curand_uniform(&local_rand_state)) / n) / max_y;
     *    ray r = cam.get_ray(u, v, &local_rand_state);   —— 和原来一模一样
     *    col += color(r, world, 50, &local_rand_state);
     * (为什么有效:随机瞎抖会扎堆、留采样盲区;每格必射 = 每个小格都有代表,同样的根数噪点更少)
     * (下面的 col / float(ns) 不用动:总数还是 ns 根光线) */
    int n = (int)sqrt((float)ns);
    for (int si = 0; si < n;si++)
    {
        for (int sj = 0; sj < n;sj++)
        {
            float u = (i + (si + curand_uniform(&local_rand_state)) / n) / max_x;
            float v = (j + (sj + curand_uniform(&local_rand_state)) / n) / max_y;
            ray r = cam.get_ray(u, v, &local_rand_state);
            col += color(r, world, 50, &local_rand_state);
        }
    }
        fb[pixel_index] = col / float(ns);
}
/* ---------- 场景构建器 ----------
 * 想自由搭场景,只需要会一件事:写一行 s.add(new sphere(...));
 * add 自动往数组尾部追加、自动计数,不用数下标,不用改任何数字。 */
struct scene
{
    hitable **list;
    int capacity;
    int count;
    __device__ scene(hitable **l, int cap) : list(l), capacity(cap), count(0) {}
    __device__ void add(hitable *object)
    {
        if (count < capacity) // 超了悄悄丢掉,免得写坏显存
            list[count++] = object;
    }
};

__global__ void create_world(hitable **d_list, hitable **d_world, int capacity, int *d_num_objects)
{
    if (threadIdx.x == 0 && blockIdx.x == 0)
    {
        scene s(d_list, capacity);

        // ===== 毕业场景:随机球大军(把你 CPU 版 main.cpp 的 random_scene 移植过来)=====
        // GPU 上也要有骰子才能造随机场景 —— 现场开一个:
        curandState st;
        curand_init(1234, 0, 0, &st); // 场景种子:想换布局改这个数字
        /* TODO 核心代码⑩:随机球场景(CPU 版在 ray tracing 项目 main.cpp 的 random_scene 函数)
         * 移植对照表:
         *   random_double()         → curand_uniform(&st)
         *   vec3::random()          → vec3(curand_uniform(&st), curand_uniform(&st), curand_uniform(&st))
         *   vec3::random(0.5, 1)    → vec3(0.5f + 0.5f*curand_uniform(&st), ... 三个分量同理)
         *   random_double(0, 0.5)   → 0.5f * curand_uniform(&st)
         *   make_shared<sphere>(...) → s.add(new sphere(...))
         * 四部分:
         * 1. 大地板:sphere(vec3(0,-1000,0), 1000, lambertian(0.5,0.5,0.5))(GPU 版没有棋盘格贴图,用纯灰)
         * 2. 22×22 小球队:a,b 从 -11 到 10:
         *    center = vec3(a + 0.9*rand, 0.2, b + 0.9*rand);
         *    离大金属球 (4,0.2,0) 太近(<0.9)就跳过;choose_mat<0.8 漫反射 / <0.95 金属 / 否则玻璃
         * 3. 三个大球:玻璃 r=1 (0,1,0); 棕漫反射 r=1 (-4,1,0); 银金属 r=1 (4,1,0) fuzz=0
         * (贴士:可以先只写大地板+三个大球,编译跑通再加 22×22 循环,不怕贪多嚼不烂) */
        // =========================================================
        s.add(new sphere(vec3(0, -1000, 0), 1000, new lambertian(vec3(0.5, 0.5, 0.5)))); // 大地板:纯灰(CPU 版的棋盘格贴图 GPU 还没有)
        s.add(new sphere(vec3(0, 1, 0), 1, new dielectric(1.5f)));                        // 大玻璃球:在 (0,1,0)
        s.add(new sphere(vec3(-4, 1, 0), 1, new lambertian(vec3(0.4, 0.2, 0.1))));        // 大棕球:在 (-4,1,0)
        s.add(new sphere(vec3(4, 1, 0), 1, new metal(vec3(0.7, 0.6, 0.5), 0)));           // 大银镜面球:在 (4,1,0)
        for (int a = -11; a <= 10;a++)
        {
            for (int b = -11; b <= 10;b++)
            {
                auto choose_mat = curand_uniform(&st);
                vec3 center = vec3(a + 0.9 * curand_uniform(&st), 0.2, b + 0.9 * curand_uniform(&st));
                if((center-vec3(4,0.2,0)).length()<0.9) continue;
                else
                {
                    if(choose_mat<0.8)
                    {
                        auto albedo = vec3(curand_uniform(&st), curand_uniform(&st), curand_uniform(&st)) * vec3(curand_uniform(&st), curand_uniform(&st), curand_uniform(&st));
                        s.add(new sphere(center, 0.2, new lambertian(albedo)));
                    }
                    else if(choose_mat<0.95)
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
        *d_world = new bvh_node(d_list, 0, s.count); // 把全部 s.count 个球建成 BVH 树,树根投进信箱
        *d_num_objects = s.count;                    // 把数量回传给 CPU(树根自己会带着全树收摊)
    }
}
__global__ void free_world(hitable **d_list, hitable **d_world, int num_objects)
{
    if (threadIdx.x == 0 && blockIdx.x == 0)
    {
        ((bvh_node *)(*d_world))->destroy(); // 树自己删:全部内部节点 + 全部叶子球(不用再逐个删 d_list[i] 了)
    }
}
int main()
{
    // 显存里每个线程的"函数调用栈"默认只有 1KB,color 递归 50 层直接撑爆(栈溢出崩溃)
    // 放大到 32KB:color 递归 50 层 × 每层又套 BVH 遍历递归约 10 层,双保险
    cudaDeviceSetLimit(cudaLimitStackSize, 32768);
    int nx = 1000, ny = 500; // 宽是1000，高是500
    int num_pixels = nx * ny;
    size_t fb_size = num_pixels * sizeof(vec3);
    vec3 *fb;                        // 只是一个数组
    cudaMallocManaged(&fb, fb_size); // 申请统一内存

    // 为几万个随机数状态机分配显存
    curandState *d_rand_state;
    cudaMalloc((void **)&d_rand_state, num_pixels * sizeof(curandState));

    //  准备创世！
    const int MAX_OBJECTS = 1024; // 场景容量:想加多少球都行,真超了再改这一个数字
    hitable **d_list;             // 依然是一个数组
    hitable **d_world;
    int *d_num_objects; // 回传:场景里实际造了几个物体(收摊时用)
    cudaMalloc((void **)&d_list, MAX_OBJECTS * sizeof(hitable *));
    cudaMalloc((void **)&d_world, sizeof(hitable *));
    cudaMalloc((void **)&d_num_objects, sizeof(int));
    create_world<<<1, 1>>>(d_list, d_world, MAX_OBJECTS, d_num_objects);
    // 决定线程数和块数
    dim3 threads(8, 8);
    dim3 blocks((nx + threads.x - 1) / threads.x, (ny + threads.y - 1) / threads.y);
    render_init<<<blocks, threads>>>(nx, ny, d_rand_state);
    cudaDeviceSynchronize(); // ⚠️ 必须等世界造完才能渲染！
    // ===== 相机:毕业图参数(书上随机场景的标准机位)=====
    camera cam(vec3(13.0f, 2.0f, 3.0f), // lookfrom:相机在哪
               vec3(0.0f, 0.0f, 0.0f),  // lookat:看哪
               vec3(0.0f, 1.0f, 0.0f),  // vup:头顶
               20.0f, 2.0f,             // vfov 视角, aspect 宽高比
               0.1f, 10.0f);            // aperture 光圈(有景深!), focus_dist 对焦距离
    // ===============================================

    // 👇 修改：传入 d_world
    int ns = 100; // 每个像素投多少根光线:根数越多黑点越少,但时间越长
    render<<<blocks, threads>>>(fb, nx, ny, ns, cam, d_world, d_rand_state);
    cudaDeviceSynchronize();
    std::cout << "P3\n"
              << nx << " " << ny << "\n255\n";
    for (int j = ny - 1; j >= 0; j--)
    {
        for (int i = 0; i < nx; i++)
        {
            size_t pixel_index = j * nx + i; // 把对应的fb的索引拿出来
            // 直接调用 vec3 类的 r(), g(), b() 方法获取颜色
            float r = fb[pixel_index].r();
            float r_1 = sqrt(r);
            float g = fb[pixel_index].g();
            float g_1 = sqrt(g);
            float b = fb[pixel_index].b();
            float b_1 = sqrt(b);
            int ir = int(255.99 * r_1); // TODO 核心代码⑧:gamma 校正 —— 取整之前给颜色开个平方根
            int ig = int(255.99 * g_1);
            int ib = int(255.99 * b_1);
            cout << ir << " " << ig << " " << ib << "\n";
        }
    }
    // 渲染完毕,收摊:把 GPU 上 new 出来的世界按记录的数量一个个删掉
    int num_objects = 0;
    cudaMemcpy(&num_objects, d_num_objects, sizeof(int), cudaMemcpyDeviceToHost);
    free_world<<<1, 1>>>(d_list, d_world, num_objects);
    cudaFree(d_num_objects);
    cudaFree(d_list);
    cudaFree(d_world);
    cudaFree(d_rand_state);
    cudaFree(fb);
}