#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#define NOMINMAX
#include <windows.h>
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

/* =====================================================================
 * main_anim.cu —— 加餐:让画面动起来(M4 交互窗口的预演)
 *
 * 和甜点(main_progressive.cu)的区别:那次是"写一堆文件自己翻着看",
 * 这次是真正的窗口播放——每帧渲染完直接画上屏幕,零文件 I/O。
 * (上次实测:写一张 PPM ≈ 1 秒,动画 60 帧光写文件就要 1 分钟,此路不通)
 *
 * 相机:毕业机位(lookfrom 13,2,3 → lookat 原点)绕 Y 轴匀速转圈,10 秒一圈。
 *   起始角度 = atan2(3,13),所以第一帧就是毕业图的机位和构图。
 * 窗口:Windows 自带 GDI(系统库,不用装任何东西),标题栏实时显示 FPS。
 *
 * 用法:
 *   anim.exe          → 4 spp + 模糊半径 2,一直播,ESC / 关窗退出
 *   anim.exe 16       → 16 spp(光线更多、更干净但更慢)
 *   anim.exe 16 0 0   → 16 spp、不模糊(纯光线硬扛)
 *   anim.exe 4 3      → 播 3 秒自动退出(验收模式,控制台打印平均帧率)
 * 参数:[spp] [秒数,0=一直播] [模糊半径,0=关]
 * 噪点账:噪点幅度 ∝ 1/√光线数 → 4→16 spp 干净 2 倍;半径 2 的模糊(5×5 窗)≈ 光线 ×25。
 * 帧率账:60Hz 预算下纯渲染最多约 5 spp,想要干净就得靠模糊/去噪——实时渲染的常态。
 * 坑:ns 必须是完全平方数(1/4/9/16...),render 里 n = (int)sqrt(ns) 取整,
 *    传 2 只会画 1 根光线——main.cu 老规矩。
 * (本文件不改动 main.cu / bvh.h,阶梯版本不受影响)
 * ===================================================================== */

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
    rand_state[pixel_index] = local_rand_state; // 关键:这一帧的骰子状态存回去,下一帧接着走
}

/* 应急去噪:盒式模糊(半径 radius 的方窗平均)
 * 原理:噪点是逐像素独立的随机抖动,把 N 个像素一平均,抖动幅度除以 √N ——
 *   5×5 的窗 = 25 个像素一平均,视觉上约等于光线数放大 25 倍(用少量柔化换干净)。
 * 代价:物体边缘会糊 radius 个像素。radius = 0 关掉;正经的边缘保持去噪器留给 M4。 */
__global__ void blur(vec3 *dst, const vec3 *src, int nx, int ny, int radius)
{
    int i = threadIdx.x + blockIdx.x * blockDim.x;
    int j = threadIdx.y + blockIdx.y * blockDim.y;
    if (i >= nx || j >= ny)
        return;
    vec3 sum(0.0f, 0.0f, 0.0f);
    int n = 0;
    for (int dj = -radius; dj <= radius; dj++)
        for (int di = -radius; di <= radius; di++)
        {
            int x = i + di;
            int y = j + dj;
            if (x < 0 || x >= nx || y < 0 || y >= ny)
                continue;
            sum += src[y * nx + x];
            n++;
        }
    dst[j * nx + i] = sum / float(n);
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

// 窗口消息:ESC 或点关窗 → 退出
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN && w == VK_ESCAPE)
    {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}

int main(int argc, char **argv)
{
    int nx = 720, ny = 360;                  // 16:9;宽高比 2.0 = 毕业机位构图原样
    int ns = (argc > 1) ? atoi(argv[1]) : 4; // 每帧采样数(必须完全平方数)
    float max_seconds = (argc > 2) ? (float)atof(argv[2]) : 0.0f; // 0 = 一直播
    int blur_radius = (argc > 3) ? atoi(argv[3]) : 2; // 应急去噪:盒式模糊半径,0 = 关

    // ===== GPU 侧:和 main.cu 一模一样的开局 =====
    cudaDeviceSetLimit(cudaLimitStackSize, 32768);
    int num_pixels = nx * ny;
    size_t fb_size = num_pixels * sizeof(vec3);
    vec3 *fb;
    cudaMalloc((void **)&fb, fb_size); // 显存帧缓冲(不再用统一内存:动画每帧要读,页迁移税 34ms/帧,见诊断)
    vec3 *fb_host;
    cudaMallocHost((void **)&fb_host, fb_size); // 钉住(pinned)的主机暂存区:每帧一次 memcpy,读它没有页故障
    vec3 *fb_blur;
    cudaMalloc((void **)&fb_blur, fb_size); // 模糊后的帧缓冲(blur 核函数的输出)

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

    // 轨道参数:水平半径 = 毕业机位到 Y 轴的距离,起始角度 = 毕业机位的方向
    const float R = sqrtf(13.0f * 13.0f + 3.0f * 3.0f);
    const float theta0 = atan2f(3.0f, 13.0f);
    const float orbit_seconds = 10.0f; // 10 秒一圈

    // ===== 开窗口(GDI,Windows 自带,不用装任何库)=====
    SetProcessDPIAware();
    WNDCLASSA wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "CudaAnimWindow";
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

    // 32 位 DIB(负高度 = 从上往下),每帧把 fb 转成 BGRA 写进这块内存,再 StretchDIBits 上屏
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

    // ===== 主循环:渲染一帧 → 转颜色 → 上屏,同时不停地抽窗口消息 =====
    MSG msg;
    int quit = 0;
    int frames = 0;
    float last_report = 0.0f;
    double sec_render = 0.0, sec_convert = 0.0, sec_blit = 0.0; // 每段耗时累计(诊断用)
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

        float theta = theta0 + 2.0f * 3.14159265f * t / orbit_seconds;
        vec3 lookfrom(R * cosf(theta), 2.0f, R * sinf(theta));
        camera cam(lookfrom, vec3(0, 0, 0), vec3(0, 1, 0),
                   20.0f, float(nx) / float(ny), 0.1f, 10.0f);
        auto t1 = chrono::steady_clock::now();
        render<<<blocks, threads>>>(fb, nx, ny, ns, cam, d_world, d_rand_state);
        if (blur_radius > 0)
            blur<<<blocks, threads>>>(fb_blur, fb, nx, ny, blur_radius);
        cudaMemcpy(fb_host, blur_radius > 0 ? fb_blur : fb, fb_size, cudaMemcpyDeviceToHost); // 顺带完成了等待(同一条流,核函数跑完才轮到它)
        auto t2 = chrono::steady_clock::now();
        sec_render += chrono::duration<double>(t2 - t1).count();

        // fb_host(线性 RGB float)→ DIB(BGRA 8bit,gamma 开根,和 main.cu 输出同款)
        // 行序要对齐:fb 第 0 行 = 图像最下面一行(main.cu 写 PPM 时从 j=ny-1 往下数,顶行在前);
        // DIB 第 0 行 = 屏幕最上面一行(负高度 = 从上往下)。倒着取,画面才不倒。
        unsigned char *p = (unsigned char *)dib_bits;
        for (int r = 0; r < ny; r++)
        {
            int src_row = ny - 1 - r; // 屏幕第 r 行 = fb 的第 (ny-1-r) 行
            for (int c = 0; c < nx; c++)
            {
                const vec3 &col = fb_host[src_row * nx + c];
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
        StretchDIBits(hdc, 0, 0, client.right, client.bottom,
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
            sprintf(title, "CUDA Ray Tracer - %.1f FPS - %d spp - %dx%d - ESC to quit", fps, ns, nx, ny);
            SetWindowTextA(hwnd, title);
            printf("t=%.1fs  frames=%d  avg %.1f FPS\n", t, frames, fps);
            fflush(stdout);
        }
    }

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
    cudaFree(fb_blur);
    cudaFreeHost(fb_host);
    DeleteObject(dib);
    DestroyWindow(hwnd);
    return 0;
}
