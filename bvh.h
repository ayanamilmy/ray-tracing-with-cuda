#ifndef BVH_H
#define BVH_H

#include "hitable.h"
#include "aabb.h"
#include <math.h>

/* =====================================================================
 * bvh.h —— GPU 版 BVH
 *
 * 阶梯第 3 级(扁平化):整棵树从"指针手拉手"改成"一整块连续数组"。
 * 三个变化:
 * 1. 节点类型 flat_node:盒 + 左右下标 + tag(0=内部,1=叶子)。
 *    叶子不再存球指针,存"球在 objects 数组里的下标"。
 * 2. 建树 build():不再 new 节点,而是往数组里"占坑拿门牌号"(next 计数器)。
 * 3. 遍历:栈里装 int 下标,不再是指针。
 * 为什么快:以前 ~977 个 new 出来的节点散落在显存各处,每访问一个都是随机的
 *   显存跳跃(缓存预取完全失效);现在是一整块连续内存,节点 36 字节一个,
 *   硬件预取器可以一路流式扫过去。
 * 铁律升级:旧版"左==右 → 叶子"(为了 destroy 区分)换成 tag 字段(新版)。
 * ===================================================================== */

/* 节点标签:内部节点还是叶子 */
const int TAG_INTERNAL = 0;
const int TAG_LEAF = 1;

/* 扁平节点:整棵树由这种节点首尾相接排成数组 */ /* 整个树就是一个结构体数组，一个数组格子里面有box和三个整数类型 */
struct flat_node
{
    aabb box;  // 这个节点管的地盘(内部节点=孩子盒子的并集;叶子=球的盒子)
    int left;  /* 内部节点:左孩子下标;叶子:球在 objects 数组里的下标 */
    int right; /* 内部节点:右孩子下标;叶子:不用 */
    int tag;   // TAG_INTERNAL 或 TAG_LEAF
};

/* ⑫b 比较尺子:比第 axis 轴上的最小角坐标(不变,⑮a 建树时照用) */
inline __device__ bool box_compare(hitable *a, hitable *b, int axis)
{
    aabb box_a, box_b;      /* 准备两个空盒子 */
    a->bounding_box(box_a); /* bounding_box是虚函数，不同的物体有不同的算法，问一下a的盒子有多大，放进box_a厘米 */
    b->bounding_box(box_b);
    return box_a.minimum[axis] < box_b.minimum[axis];
    /* 比的是最小角在第 axis 轴上的坐标。谁的最小角靠左(坐标小), 谁排前面。返回 true = "a 该在 b 前面", false = "b 该在 a 前面"。 */
}

/* ⑫c 插入排序:把 key 往前插到合适位置(它根本不"交换",只"挪"和"插")
 * 把 objects[start..end) 按 axis 轴排成一行,从小到大。
 * 写法:for i = start+1 .. end-1:key = objects[i],j = i-1;
 *       当 j >= start 且 key 比 objects[j] 小(box_compare(key, objects[j]) 为真):往后挪,j--
 *       最后 objects[j+1] = key; */
inline __device__ void sort_objects(hitable **objects, int start, int end, int axis)
{
    for (int i = start + 1; i <= end - 1; i++)
    {
        hitable *key = objects[i]; // ① 新牌先抽出来揣兜里(上次的野指针坑:key 必须先赋值!)
        int j = i - 1;
        // ② 左边那张比 key 大(尺子问法:box_compare(key, objects[j]) = "key 该不该排在它前面"),
        /*    就把它右移一格腾位子,继续往左看 —— 你的注释"key 小于物体"方向完全正确,上次只是代码参数顺序反了 */
        while (j >= start && box_compare(key, objects[j], axis))
        {
            objects[j + 1] = objects[j]; /* 右移 = 覆盖,不是交换 */
            j--;
        }
        objects[j + 1] = key; /* ③ 兜里的牌放进腾出的空位 */
    }
}

/* TODO 核心代码⑮a:建树 build —— 把 ⑫d 的四步从"new 散装节点"翻译成"往数组里占坑"
 * 签名:__device__ int build(hitable **objects, int start, int end, flat_node *nodes, int &next)
 * 返回值:我这棵子树根节点的下标。
 *
 * 和 ⑫d 唯一的区别:以前 new bvh_node(...) 造一个节点的地方,现在变成两步:
 *   ① int my = next++; 抢一个坑(门牌号归我,next 是引用 = 全树共用一本户口本)
 *   ② 把数据填进 nodes[my]。
 *
 * 六步:
 * 1. int my = next++;   // 进来第一件事:占坑
 * 2. 算整段 bounds、挑最长轴(dx/dy/dz、axis):和 ⑫d 完全一样
 * 3. span == 1 → 叶子:
 *    nodes[my].tag = TAG_LEAF;
 *    nodes[my].left = start;   // 记住"球在 objects 数组里的下标"(right 不用)
 *    objects[start]->bounding_box(nodes[my].box);   // 自己的盒子 = 球的盒子
 * 4. span == 2 → 两个真叶子(和 ⑫d 一样):
 *    nodes[my].tag = TAG_INTERNAL;
 *    nodes[my].left  = build(objects, start, start+1, nodes, next);
 *    nodes[my].right = build(objects, start+1, end,    nodes, next);
 *    (递归 build 的返回值就是孩子的门牌号,抄下来)
 * 5. 否则:sort_objects(objects, start, end, axis);
 *    int mid = start + span / 2;
 *    nodes[my].tag = TAG_INTERNAL;
 *    nodes[my].left  = build(objects, start, mid, nodes, next);
 *    nodes[my].right = build(objects, mid, end,    nodes, next);
 * 6. 组装自己的盒子(和 ⑫d 一样):
 *    nodes[my].box = surrounding_box(nodes[nodes[my].left].box, nodes[nodes[my].right].box);
 *    return my;
 */
__device__ int build(hitable **objects, int start, int end, flat_node *nodes, int &next)
{
    int my = next++; /* my就是数组下标，先占个位置。my=next,next再加加 */
    aabb bounds;
    objects[start]->bounding_box(bounds);
    for (int i = start + 1; i <= end - 1; i++)
    {
        aabb temp;
        objects[i]->bounding_box(temp);
        bounds = surrounding_box(temp, bounds);/*计算当前节点所有负责物体的包围盒 */
    }
    float dx = bounds.maximum.x() - bounds.minimum.x();
    float dy = bounds.maximum.y() - bounds.minimum.y();
    float dz = bounds.maximum.z() - bounds.minimum.z();
    int axis = dx >= dy && dx >= dz ? 0 : (dy >= dz ? 1 : 2);
    int span = end - start;
    if (span == 1) /* 如果是叶子 */
    {
        nodes[my].tag = TAG_LEAF;
        nodes[my].left = start;
        objects[start]->bounding_box(nodes[my].box);
        return my; /* ← 加这一行,叶子跳过下面的组装 */
    }
    else if (span == 2)
    {
        nodes[my].tag = TAG_INTERNAL;
        nodes[my].left = build(objects, start, start + 1, nodes, next);/*左孩子递归 */
        nodes[my].right = build(objects, start + 1, end, nodes, next);/*右孩子递归 */
    }
    else
    {
        sort_objects(objects, start, end, axis);
        int mid = start + span / 2;
        nodes[my].tag = TAG_INTERNAL;
        nodes[my].left = build(objects, start, mid, nodes, next);/*返回左孩子的下标 */
        nodes[my].right = build(objects, mid, end, nodes, next);/*返回右孩子的下标 */
    }
    nodes[my].box = surrounding_box(nodes[nodes[my].left].box, nodes[nodes[my].right].box);/*组装左右盒子 */
    return my;
    
}//这个是前序遍历，一个节点的右子节点的下标是左子树全部遍历完之后再加一

class bvh_node : public hitable
{
public:
    flat_node *nodes;  // 整棵树:一大块连续数组
    int root;          // 根节点在数组里的下标
    int num_nodes;     // 实际用掉了几个节点(destroy 收摊用)
    hitable **objects; // 记住球们住在哪(叶子测球要用)

    __device__ bvh_node(hitable **objs, int start, int end)
    {
        objects = objs;
        nodes = new flat_node[2 * (end - start) + 1]; /* 全树最多 2N-1 个节点,多一个兜底 */
        int next = 0;
        root = build(objects, start, end, nodes, next); /* ⑮a 的 build 在上面 */
        num_nodes = next;                               /* 记录实际用掉的节点数 */
    }

    /* TODO 核心代码⑮b:扁平数组上的遍历 —— ⑭a 的栈 + ⑭b 的近先,全部照搬,只做三处翻译:
     *   const bvh_node *stack[32] → int stack[32]      (装节点下标,128 字节,还更小了)
     *   指针 node                  → nodes[idx]
     *   孩子指针                   → nodes[idx].left / nodes[idx].right(下标)
     *   叶子测球 node->left->hit  → objects[nodes[idx].left]->hit
     * 其余一模一样:
     *   pop → nodes[idx].box.hit 盒测(不过就 continue)→ 按 tag 分拣:
     *     叶子:测球;命中 → t_max = rec.t(红线收紧)+ hit_anything = true
     *     内部:⑭b 的近先(hit_entry 测两孩子的盒子 → 四种情况 push 下标)
     * 循环骨架、近裁剪红线、先 push 远再 push 近、四种 push 情况:全部原样照搬。
     */
    __device__ virtual bool hit(const ray &r, float t_min, float t_max, hit_record &rec) const
    {
        bool hit_anything = false;
        int stack[32];
        int top = 0;
        stack[top++] = root; /* 先把根节点压栈 */
        while (top > 0)      /* 开始遍历 */
        {
            int idx= stack[--top]; /* 把栈顶的地址拿出来处理 */
            flat_node node = nodes[idx];/*把这个节点的整个都拿出来 */
            if (!node.box.hit(r, t_min, t_max))
                continue; /* 没碰到的话直接剪枝不要了 */
            else
            {
                if (node.tag == TAG_LEAF) /* 到叶子节点了 */
                {
                    if (objects[node.left]->hit(r, t_min, t_max, rec)) /* 如果叶子结点的球被打中了，这时候击中的时间已经被记录了 */
                    {
                        t_max = rec.t; /* 更新一下记录的时间，更远的不用打 */
                        hit_anything = true;
                    }
                }
                else /* 内部节点 */
                {
                    /* TODO 核心代码⑭b(下):近先遍历(先探进门早的孩子)
                     * 为什么快:进门早的孩子先被探 → 命中后红线 t_max 立刻收紧 →
                     *           进门晚的孩子弹出时,顶部盒测(用收紧后的 t_max)大概率不过 → 整棵远子树白赚剪枝。
                     * 为什么结果不变:盒测不耗随机数,答案只由"最近的 t"决定,与探测先后顺序无关。
                     *
                     * 四步:
                     * 1. 看孩子真身(铁律:内部节点的孩子一定是节点):
                     *    const bvh_node *childL = (const bvh_node *)node->left;
                     *    const bvh_node *childR = (const bvh_node *)node->right;
                     *    (光线参数本来就叫 r,右孩子起名 childR 免得撞车)
                     * 2. 两个孩子各做一次"带进门时间"的盒测(⑭b(上)的新函数):
                     *    float tL, tR;
                     *    bool passL = childL->box.hit_entry(r, t_min, t_max, tL);
                     *    bool passR = childR->box.hit_entry(r, t_min, t_max, tR);
                     * 3. 按四种情况 push:
                     *    都过:谁进门早谁后 push(后入先出 → 先进门的先被掏出来):
                     *          tL < tR → 先 push childR,再 push childL
                     *          否则    → 先 push childL,再 push childR
                     *    只过左 → push childL;只过右 → push childR;
                     *    都不 → 谁都不 push(白赚的剪枝)
                     * 4. 没有第 4 步,就这么短。
                     * (盒测过的孩子弹出时循环顶部会再测一次——那时的 t_max 可能更紧,再测 = 剪枝)
                     */
                    /* 你的代码 */
                    int childL = node.left;
                    int childR = node.right;       /* 把两个孩子的情况区分一下 */
                    float tL, tR;                                           /* 用来装时间的变量准备一下 */
                    bool passL = nodes[childL].box.hit_entry(r, t_min, t_max, tL);
                    bool passR = nodes[childR].box.hit_entry(r, t_min, t_max, tR);
                    if (passL == passR && passL == true) /* 都过的话,谁进门早,谁后进栈，后进先出 */
                    {
                        if (tL <= tR)
                        {
                            stack[top++] = childR;
                            stack[top++] = childL;
                        }
                        else if (tL > tR)
                        {
                            stack[top++] = childL;
                            stack[top++] = childR;
                        }
                    }
                    else if (passL == true && passR == false)
                    {
                        stack[top++] = childL;
                    }
                    else if (passR == true && passL == false)
                    {
                        stack[top++] = childR;
                    }
                }
            }
        }

        return hit_anything;
    }

    /* 收摊:每个球恰好属于一片叶子,各删一次;再删整块数组 */
    __device__ void destroy()
    {
        for (int i = 0; i < num_nodes; i++)
            if (nodes[i].tag == TAG_LEAF)
                delete objects[nodes[i].left];
        delete[] nodes;
        delete this;
    }

    __device__ virtual bool bounding_box(aabb &output_box) const
    {
        output_box = nodes[root].box;
        return true;
    }
};

#endif
