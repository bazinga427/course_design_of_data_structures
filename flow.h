#pragma once

#include "graph.h"

#include <utility>
#include <vector>

// ============================================================
//  最大流 / 最小割（Ford-Fulkerson 算法）
//
//  算法思路（就是视频里那套）：
//    1. 残留网络一开始就是容量矩阵本身
//    2. 在残留网络里找一条从 s 到 t 的增广路（这条路上每条边的剩余容量都 > 0）
//    3. 这条路能推多少流 = 路径上最小的剩余容量（瓶颈）
//    4. 沿路把正向边的剩余容量减掉瓶颈，反向边的剩余容量加上瓶颈
//    5. 回到第 2 步，直到找不到增广路为止，此时的总流量就是最大流
//
//  最小割顺便就出来了：在最后的残留网络里，从 s 出发能走到的点组成 S，
//  走不到的点组成 T，从 S 指向 T 的边就是割边，它们的容量和 == 最大流。
//  （这就是最大流最小割定理，代码里会自己检查一遍，答辩时可以直接拿来说）
// ============================================================

// 一次增广的过程，做动画的时候按这个一步步播
struct AugmentStep {
    std::vector<int> path;      // 这次找到的增广路：s -> ... -> t
    int bottleneck = 0;         // 这条路上能推多少流
    int flow_after = 0;         // 推完这一趟之后的总流量
};

struct FlowResult {
    int value = 0;                                  // 最大流的值
    std::vector<AugmentStep> steps;                 // 每次增广的过程（动画用）
    std::vector<std::vector<int>> flow;             // flow[u][v]：边 u->v 实际流过的量
    std::vector<std::vector<int>> residual;         // 最后的残留网络
    std::vector<int> source_side;                   // 最小割的 S 侧（含 s）
    std::vector<int> sink_side;                     // 最小割的 T 侧（含 t）
    std::vector<std::pair<int, int>> cut_edges;     // 割边，每个是 (u, v)，u 在 S、v 在 T
    int cut_capacity = 0;                           // 割边容量之和，应该等于 value
};

// 有向图按有向边算；无向图的容量矩阵是对称的，两个方向各算一条独立的弧
FlowResult max_flow_min_cut(const Graph& g, int s, int t);

// 把过程打到控制台：每一趟增广路、最大流、最小割、每条边的流量
void print_flow_result(const Graph& g, const FlowResult& result, int s, int t);
