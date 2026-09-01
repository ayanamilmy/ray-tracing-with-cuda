#include <iostream>
#include <fstream>
#include <cstdio>
#include <math.h>
#include "vec3.h"
#include "ray.h"
#include "hitable_list.h"
#include "sphere.h"
#include "bvh.h"
#include "camera.h"
#include <curand_kernel.h>
using namespace std;

/* =====================================================================
 * main_progressive.cu —— 甜点:渐进渲染实验(M4 交互窗口的预演)
 *
 * 和 main.cu 的区别只有两处:
 * 1. render 不除以样本数,而是累加进 fb;每批渲染"10×10 分层网格的整整一行"
 *    (10 个样本),所以第 k 批的样本 = 一次性渲染 100 根时的第 10k~10k+9 根 ——
 *    每一帧都是最终画面的"真实前缀",不是另一场渲染。
 * 2. 每批结束后把随机数状态写回 rand_state(原版 RTIOW 就这么写;
 *    main.cu 只发射一次所以漏了也无所谓,渐进版必须写回,不然每批都从同一个状态重画)。
 * 10 批 → 10 帧:10、20、…、100 spp。
 * (本文件不改动 main.cu,阶梯版本不受影响)
 * ===================================================================== */

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

// 渲染 10×10 分层网格的第 si_row 行(整行 10 个样本),累加进 fb
// 样本的随机数顺序和 main.cu 一次性渲染 100 根时逐位相同(行主序 si 外层)
__global__ void render_sum_row(vec3 *fb, int max_x, int max_y, int n, int si_row, camera cam, hitable **world, curandState *rand_state)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int j = threadIdx.y + blockIdx.y * blockDim.y;
    if (i >= max_x || j >= max_y)
        return;
    int pixel_index = j * max_x + i;

    curandState local_rand_state = rand_state[pixel_index];
    vec3 col(0.0f, 0.0f, 0.0f);
    for (int sj = 0; sj < n; sj++)
    {
        float u = (i + (si_row + curand_uniform(&local_rand_state)) / n) / max_x;
        float v = (j + (sj + curand_uniform(&local_rand_state)) / n) / max_y;
        ray r = cam.get_ray(u, v, &local_rand_state);
        col += color(r, world, 50, &local_rand_state);
    }
    fb[pixel_index] += col;
    rand_state[pixel_index] = local_rand_state; // 关键:走完这一批的状态存回去,下一批接着走
}

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
        curand_init(1234, 0, 0, &st); // 场景种子:和 main.cu 一致,场景布局逐字节相同
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

// 把"总和 ÷ 总样本数"写成 PPM(和 main.cu 的输出格式完全一致)
void dump_ppm(const vec3 *fb, int nx, int ny, float inv_total, const char *path)
{
    std::ofstream f(path);
    f << "P3\n"
      << nx << " " << ny << "\n255\n";
    for (int j = ny - 1; j >= 0; j--)
    {
        for (int i = 0; i < nx; i++)
        {
            size_t pixel_index = j * nx + i;
            float r = fb[pixel_index].r() * inv_total;
            float g = fb[pixel_index].g() * inv_total;
            float b = fb[pixel_index].b() * inv_total;
            f << int(255.99 * sqrt(r)) << " "
              << int(255.99 * sqrt(g)) << " "
              << int(255.99 * sqrt(b)) << "\n";
        }
    }
    f.close();
}

int main()
{
    cudaDeviceSetLimit(cudaLimitStackSize, 32768);
    int nx = 1000, ny = 500;
    int num_pixels = nx * ny;
    size_t fb_size = num_pixels * sizeof(vec3);
    vec3 *fb;
    cudaMallocManaged(&fb, fb_size);
    cudaMemset(fb, 0, fb_size); // 总和从 0 开始攒

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

    camera cam(vec3(13.0f, 2.0f, 3.0f),
               vec3(0.0f, 0.0f, 0.0f),
               vec3(0.0f, 1.0f, 0.0f),
               20.0f, 2.0f,
               0.1f, 10.0f);

    // 10 批:每批渲染 10×10 分层网格的一整行(10 个样本),累计 10 → 100
    int n = 10;
    int total = 0;
    for (int row = 0; row < n; row++)
    {
        render_sum_row<<<blocks, threads>>>(fb, nx, ny, n, row, cam, d_world, d_rand_state);
        cudaDeviceSynchronize();
        total += n;
        char name[64];
        sprintf(name, "prog_%03d.ppm", total);
        dump_ppm(fb, nx, ny, 1.0f / total, name);
        std::cout << "pass " << (row + 1) << "/10:+" << n << " spp → 累计 " << total << " spp → " << name << std::endl;
    }

    int num_objects = 0;
    cudaMemcpy(&num_objects, d_num_objects, sizeof(int), cudaMemcpyDeviceToHost);
    free_world<<<1, 1>>>(d_list, d_world, num_objects);
    cudaFree(d_num_objects);
    cudaFree(d_list);
    cudaFree(d_world);
    cudaFree(d_rand_state);
    cudaFree(fb);
    return 0;
}
