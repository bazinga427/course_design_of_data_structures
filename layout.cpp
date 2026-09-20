// ============================================================
//  layout.cpp
//  节点的自动布局算法：圆环布局、随机布局、力导向布局。
//
//  这里只管「把每个点放在哪个位置」，不碰任何图形库，
//  所以可以单独拿出来测试，也可以被别的模块复用。
// ============================================================

#include "layout.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>
#include <utility>

namespace {

const double kPi = 3.14159265358979323846;

// 相邻两个节点之间大致留多少距离（世界坐标单位）。
// 节点半径是 26，这里给 76 左右，画出来两个圆不会挤在一起。
const double kSpacing = 76.0;

}  // namespace

void layout_circle(std::vector<NodePos>& pos) {
    const int n = static_cast<int>(pos.size());
    if (n == 0) {
        return;
    }
    if (n == 1) {
        pos[0].x = 0.0;
        pos[0].y = 0.0;
        return;
    }

    // 让圆周上相邻两点的弧长不小于 kSpacing，点多了圆就自己变大
    const double radius = std::max(150.0, kSpacing * n / (2.0 * kPi));
    for (int i = 0; i < n; ++i) {
        const double angle = 2.0 * kPi * i / n - kPi / 2.0;   // 从正上方开始，顺时针排
        pos[i].x = radius * std::cos(angle);
        pos[i].y = radius * std::sin(angle);
    }
}

void layout_random(std::vector<NodePos>& pos, unsigned int seed) {
    const int n = static_cast<int>(pos.size());
    if (n == 0) {
        return;
    }

    std::mt19937 rng(seed);
    const double half = std::max(200.0, 60.0 * std::sqrt(static_cast<double>(n)));
    std::uniform_real_distribution<double> dist(-half, half);
    for (NodePos& p : pos) {
        p.x = dist(rng);
        p.y = dist(rng);
    }
}

void layout_force(std::vector<NodePos>& pos, const Graph& g, int iterations) {
    const int n = static_cast<int>(pos.size());
    if (n == 0) {
        return;
    }
    if (n == 1) {
        pos[0].x = 0.0;
        pos[0].y = 0.0;
        return;
    }
    if (iterations < 1) {
        iterations = 1;
    }

    // 第一次自动布局时所有点都在原点（完全重合），这种起点算出来的斥力方向没有意义，
    // 所以先摆成一个圆再迭代。已经拖过节点的图会保留当前位置，不会被这一段覆盖。
    double spread = 0.0;
    for (int i = 0; i < n; ++i) {
        spread = std::max(spread, std::hypot(pos[i].x - pos[0].x, pos[i].y - pos[0].y));
    }
    if (spread < 1e-6) {
        layout_circle(pos);
    }

    // 编号 -> 数组下标：邻接表里存的是编号，这里要换成下标才好算
    std::unordered_map<int, int> index_of;
    index_of.reserve(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i) {
        index_of[pos[i].id] = i;
    }

    // 边表（下标形式）。无向图在邻接表里存了两遍，这里只用一遍。
    std::vector<std::pair<int, int>> edges;
    edges.reserve(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i) {
        const int u = pos[i].id;
        if (u < 0 || u >= static_cast<int>(g.adj_list.size())) {
            continue;
        }
        for (int v : g.adj_list[u]) {
            const auto it = index_of.find(v);
            if (it == index_of.end()) {
                continue;                       // 编号没出现在节点表里，跳过
            }
            const int j = it->second;
            if (i == j) {
                continue;                       // 自环不参与力计算
            }
            if (!g.is_directed && j < i) {
                continue;                       // 无向边只保留一份
            }
            edges.push_back(std::make_pair(i, j));
        }
    }

    const double k = kSpacing * 1.6;            // 理想边长

    // 初始「温度」：每一轮每个点最多移动这么多距离，越往后越小（退火）
    double temperature = 0.18 * k * std::sqrt(static_cast<double>(n));
    const double cooling = std::pow(0.02, 1.0 / iterations);   // 结束时温度降到 2%

    std::vector<double> dx(n, 0.0), dy(n, 0.0);

    for (int step = 0; step < iterations; ++step) {
        std::fill(dx.begin(), dx.end(), 0.0);
        std::fill(dy.begin(), dy.end(), 0.0);

        // 1) 任意两点互相排斥，力的大小 = k^2 / d
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double vx = pos[i].x - pos[j].x;
                double vy = pos[i].y - pos[j].y;
                double d2 = vx * vx + vy * vy;
                if (d2 < 1e-6) {
                    // 两个点完全重合：给一个固定的小偏移，避免后面除以 0
                    vx = 0.37 + 0.01 * (i - j);
                    vy = 0.21 - 0.01 * (i - j);
                    d2 = vx * vx + vy * vy;
                }
                const double d = std::sqrt(d2);
                const double f = k * k / d;
                const double ux = vx / d;
                const double uy = vy / d;
                dx[i] += ux * f;
                dy[i] += uy * f;
                dx[j] -= ux * f;
                dy[j] -= uy * f;
            }
        }

        // 2) 有边相连的两点互相吸引，力的大小 = d^2 / k
        for (const auto& e : edges) {
            const int i = e.first;
            const int j = e.second;
            const double vx = pos[i].x - pos[j].x;
            const double vy = pos[i].y - pos[j].y;
            const double d = std::sqrt(vx * vx + vy * vy);
            if (d < 1e-6) {
                continue;
            }
            const double f = d * d / k;
            const double ux = vx / d;
            const double uy = vy / d;
            dx[i] -= ux * f;
            dy[i] -= uy * f;
            dx[j] += ux * f;
            dy[j] += uy * f;
        }

        // 3) 按当前温度限幅移动
        for (int i = 0; i < n; ++i) {
            const double d = std::sqrt(dx[i] * dx[i] + dy[i] * dy[i]);
            if (d < 1e-9) {
                continue;
            }
            const double step_len = std::min(d, temperature);
            pos[i].x += dx[i] / d * step_len;
            pos[i].y += dy[i] / d * step_len;
        }

        temperature *= cooling;
    }
}

void layout_normalize(std::vector<NodePos>& pos, double radius) {
    if (pos.empty() || radius <= 0.0) {
        return;
    }

    // 用重心当中心，免得整张图偏向一边
    double cx = 0.0, cy = 0.0;
    for (const NodePos& p : pos) {
        cx += p.x;
        cy += p.y;
    }
    cx /= static_cast<double>(pos.size());
    cy /= static_cast<double>(pos.size());

    double max_dist = 0.0;
    for (const NodePos& p : pos) {
        max_dist = std::max(max_dist, std::hypot(p.x - cx, p.y - cy));
    }

    if (max_dist < 1e-9) {
        // 所有点重合（例如只有一个点），只要挪到原点就行
        for (NodePos& p : pos) {
            p.x -= cx;
            p.y -= cy;
        }
        return;
    }

    const double s = radius / max_dist;
    for (NodePos& p : pos) {
        p.x = (p.x - cx) * s;
        p.y = (p.y - cy) * s;
    }
}
