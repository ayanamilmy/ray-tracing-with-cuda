#ifndef BVH_H
#define BVH_H

#include "hitable.h"
#include "aabb.h"
#include <math.h>

/* =====================================================================
 * bvh.h —— GPU 版 BVH(把你 CPU 版的 BVH.h 移植到 GPU)
 *
 * 和 CPU 版结构几乎一样,三个不同点(都是 GPU 的规矩):
 * 1. std::sort 在 GPU 上不存在 → 手写插入排序(几百个元素,绰绰有余)
 * 2. rand()%3 选轴 → 改成"最长轴"(不消耗场景随机数,球布局才能和暴力版完全一致;
 *    而且"沿最长方向切"是比随机更好的经典选择)
 * 3. span==2 拆成两个真叶子 → 全树铁律:左==右 → 叶子(球),否则 → 内部节点
 *    (收摊删除时靠这条铁律区分"球"和"树",不会把球误当树递归进去)
 * ===================================================================== */

/* TODO 核心代码⑫b:比较尺子 —— 把你 CPU 版 box_compare 搬过来(shared_ptr 换成裸指针)
 * aabb box_a, box_b;
 * a->bounding_box(box_a);  b->bounding_box(box_b);
 * return box_a.minimum[axis] < box_b.minimum[axis]; */
inline __device__ bool box_compare(hitable *a, hitable *b, int axis)
{
    aabb box_a, box_b;      // 准备两个空盒子
    a->bounding_box(box_a); // bounding_box是虚函数，不同的物体有不同的算法，问一下a的盒子有多大，放进box_a厘米
    b->bounding_box(box_b);
    return box_a.minimum[axis] < box_b.minimum[axis];
    // 比的是最小角在第 axis 轴上的坐标。谁的最小角靠左(坐标小), 谁排前面。返回 true = "a 该在 b 前面", false = "b 该在 a 前面"。
} // 我没有看懂这个函数是什么意思。26 27 28三行都看不懂

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
        //    就把它右移一格腾位子,继续往左看 —— 你的注释"key 小于物体"方向完全正确,上次只是代码参数顺序反了
        while (j >= start && box_compare(key, objects[j], axis))
        {
            objects[j + 1] = objects[j]; // 右移 = 覆盖,不是交换
            j--;
        }
        objects[j + 1] = key; // ③ 兜里的牌放进腾出的空位
    }
}

class bvh_node : public hitable
{
public:
    hitable *left;
    hitable *right;
    aabb box;

    /* 建树:递归把 objects[start..end) 切成一颗树(CPU 版结构照搬,new 在 GPU 上就是 device new) */
    __device__ bvh_node(hitable **objects, int start, int end) // objects[start]到objects[end]的部分来排成一个树,这就是构造函数，建树的构造函数
    {
        /* TODO 核心代码⑫d:建树四步
         * 1. 先算整段的总包围盒,挑最长轴:
         *    aabb bounds;  objects[start]->bounding_box(bounds);
         *    循环 i = start+1 .. end-1:拿盒子,surrounding_box 合并进 bounds
         *    dx = bounds.maximum.x() - bounds.minimum.x();dy、dz 同理;
         *    axis = dx >= dy && dx >= dz ? 0 : (dy >= dz ? 1 : 2);
         * 2. int span = end - start;
         *    span == 1 → left = right = objects[start];(叶子!)
         *    span == 2 → left = new bvh_node(objects, start, start+1);
         *                right = new bvh_node(objects, start+1, start+2);(两个真叶子)
         * 3. 否则:sort_objects(objects, start, end, axis);
         *    int mid = start + span / 2;
         *    left  = new bvh_node(objects, start, mid);
         *    right = new bvh_node(objects, mid, end);
         * 4. 组装自己的盒子:aabb box_left, box_right;
         *    left->bounding_box(box_left);  right->bounding_box(box_right);
         *    box = surrounding_box(box_left, box_right); */

        aabb bounds;
        objects[start]->bounding_box(bounds);
        for (int i = start + 1; i <= end - 1; i++)
        {
            aabb temp;
            objects[i]->bounding_box(temp);
            bounds = surrounding_box(temp, bounds);
        }
        float dx = bounds.maximum.x() - bounds.minimum.x();
        float dy = bounds.maximum.y() - bounds.minimum.y();
        float dz = bounds.maximum.z() - bounds.minimum.z();
        int axis = dx >= dy && dx >= dz ? 0 : (dy >= dz ? 1 : 2); // 这行看不懂

        int span = end - start;
        if (span == 1)
            left = right = objects[start]; // 如果递归到这里剩下一个下标了，那么就到叶子了。左右节点都是本身
        else if (span == 2)
        {
            left = new bvh_node(objects, start, start + 1);
            right = new bvh_node(objects, end - 1, end);
        }
        else //不是这两种情况，那么就先排序，再劈成两半，再递归
        {
            sort_objects(objects, start, end, axis);
            int mid = start + span / 2;
            left = new bvh_node(objects, start, mid);
            right = new bvh_node(objects, mid, end);//注意，left和right都是指针，建树就是把一堆指针建好了而已，后面还有包装盒子
        }
        aabb box_left, box_right;
        left->bounding_box(box_left);
        right->bounding_box(box_right);
        box = surrounding_box(box_left, box_right); // 然后把弄好的指针弄成一个大盒子
    }

    /* TODO 核心代码⑫e:树的遍历 —— 把你 CPU 版 hit 原样搬过来
     * 1. if (!box.hit(r, t_min, t_max)) return false;   —— 先问自己的盒子,没穿过整棵子树直接放弃
     * 2. bool hit_left = left->hit(r, t_min, t_max, rec);
     * 3. bool hit_right = right->hit(r, t_min, hit_left ? rec.t : t_max, rec);  —— 近裁剪!
     * 4. return hit_left || hit_right; */
    /* TODO 核心代码⑭a:递归遍历 → 显式栈循环(老规矩:你写代码)
     * 背景:递归版每钻进一个孩子节点都是一次真正的函数调用(压栈/弹栈、寄存器上下文全要动,
     *       还可能溢出到 DRAM)。光线在树里要走几十次岔路,这些开销×几十次就是 8.2 秒的大头。
     *       显式栈 = 口袋里的一张"路线纸":把待办节点记在纸上,循环一张张掏出来办,
     *       函数调用栈的事(CSAPP 老熟人)自己接管,一条函数从头走到尾。
     *
     * 三样装备:
     * 1. const bvh_node *stack[32]; int top = 0;
     *    —— 路线纸。铁律保证:内部节点的孩子一定也是节点(左==右的叶子当场测完,从不入栈),
     *       所以纸上只会有 bvh_node*,不用装 hitable*。树深对 488 球只有 ~10 层,32 格绰绰有余;
     *       数组开小,编译器常常能整段塞进寄存器。
     * 2. bool hit_anything = false;
     *    —— 全程有没有打中过(最后返回它)。
     * 3. 近裁剪:t_max 是"最远允许距离"。递归版靠 hit_left ? rec.t : t_max 把右孩子收紧;
     *       循环版更简单:只要某次测出命中(rec 被更新),马上 t_max = rec.t ——
     *       之后所有盒测/球测自动更苛刻,栈里排队的远盒子盒测直接不过,自然剪枝。t_min 全程不动。
     *
     * 循环骨架:
     *   stack[top++] = this;                        // 根先上纸(根是叶子也走同一套,循环里会分拣)
     *   while (top > 0)
     *   {
     *       const bvh_node *node = stack[--top];    // 掏栈顶一张(后入先出 = 最近待办)
     *       if (!node->box.hit(r, t_min, t_max))    // 和递归版开头的盒测一模一样
     *           continue;                           // 光线没穿过这个盒子 → 整棵子树放弃
     *       if (node->left == node->right)
     *       {
     *           // 叶子(铁律):直接测球。只测一次 —— 顺手修掉递归版"同一个球测两遍"的浪费
     *           if (node->left->hit(r, t_min, t_max, rec)) { t_max = rec.t; hit_anything = true; }
     *       }
     *       else
     *       {
     *           // 内部节点:两个孩子上纸。先 push 右再 push 左 → 左先被掏出
     *           //(和递归版"先探左、再探右"的顺序一致)
     *           stack[top++] = (const bvh_node *)node->right;
     *           stack[top++] = (const bvh_node *)node->left;
     *       }
     *   }
     *   return hit_anything;
     *
     * 两个想明白才动手的点:
     * ① 为什么不能"一命中就 return true":第一个命中的不一定最近 —— 更近的兄弟还在纸上排队,
     *    必须靠收紧后的 t_max 把它们一个个淘汰掉。
     * ② 为什么结果逐字节不变:盒测/球测都不消耗随机数,最终答案只由"最近的 t"决定,与先后顺序无关;
     *    递归版的双测删掉也不会改结果(第二次测的距离=第一次的命中距离,正好卡在 t_max 上,必然落空)。
     */



    __device__ virtual bool hit(const ray &r, float t_min, float t_max, hit_record &rec) const
    {
        bool hit_anything=false;
        const bvh_node *stack[32];
        int top = 0;
        stack[top++] = this;//先把根节点压栈
        while(top>0)//开始遍历
        {
            const bvh_node *node = stack[--top];//把栈顶的地址拿出来处理
            if(!node->box.hit(r,t_min,t_max))
                continue;//没碰到的话直接剪枝不要了
            else
            {
                if(node->left==node->right)//到叶子节点了
                {
                    if(node->left->hit(r,t_min,t_max,rec))//如果叶子结点的球被打中了，这时候击中的时间已经被记录了
                    {
                        t_max = rec.t;//更新一下记录的时间，更远的不用打
                        hit_anything = true;
                    }
                }
                else//还没有到叶子结点
                {
                    stack[top++] = (const bvh_node *)node->right;
                    stack[top++] = (const bvh_node *)node->left;//把右孩子和左孩子分别压入栈中
                }
            }
        }

             return hit_anything; // 占位:写完⑭a删掉这行
    }

    /* 收摊:树负责删掉全部内部节点和叶子球(铁律:左==右 → 叶子,球只删一次) */
    __device__ void destroy()
    {
        if (left == right)
        {
            delete left;
        }
        else
        {
            ((bvh_node *)left)->destroy();
            ((bvh_node *)right)->destroy();
        }
        delete this;
    }

    __device__ virtual bool bounding_box(aabb &output_box) const
    {
        output_box = box;
        return true;
    }
};

#endif
