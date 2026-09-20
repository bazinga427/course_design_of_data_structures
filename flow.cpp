#include "flow.h"

#include <iostream>
#include <queue>
#include <vector>

using namespace std;

namespace {

// 在残留网络里用 DFS 找一条从 u 到 t 的增广路。
// 走到 t 就说明找到了，路径存在 path 里（path[0] 是源点）。
bool dfs_find_path(int u, int t, const vector<vector<int>>& residual,
                   vector<bool>& visited, vector<int>& path) {
    if (u == t) return true;
    visited[u] = true;

    int n = static_cast<int>(residual.size()) - 1;
    for (int v = 1; v <= n; ++v) {
        if (residual[u][v] > 0 && !visited[v]) {
            path.push_back(v);
            if (dfs_find_path(v, t, residual, visited, path)) return true;
            path.pop_back();            // 这个方向走不通，退回来试别的点
        }
    }
    return false;
}

// 一条增广路能推多少流 = 路上最小的剩余容量
int bottleneck_of(const vector<vector<int>>& residual, const vector<int>& path) {
    int best = -1;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        int cap = residual[path[i]][path[i + 1]];
        if (best < 0 || cap < best) best = cap;
    }
    return best < 0 ? 0 : best;
}

void print_path(const vector<int>& path) {
    for (size_t i = 0; i < path.size(); ++i) {
        cout << path[i];
        if (i + 1 < path.size()) cout << " -> ";
    }
}

}  // namespace

FlowResult max_flow_min_cut(const Graph& g, int s, int t) {
    FlowResult result;
    int n = g.n;

    result.flow.assign(n + 1, vector<int>(n + 1, 0));
    result.residual.assign(n + 1, vector<int>(n + 1, 0));

    if (n < 2 || s < 1 || s > n || t < 1 || t > n || s == t) {
        cout << "[error] bad source/sink: need 1 <= s,t <= " << n << " and s != t" << endl;
        return result;
    }

    // 残留网络一开始就等于容量矩阵
    vector<vector<int>> residual = g.cap_matrix;

    // 理论上整数容量一定会结束，这里只是防止极端情况跑太久（比如容量特别大）
    const size_t SAFETY_LIMIT = 200000;

    while (result.steps.size() < SAFETY_LIMIT) {
        // 1. 找一条增广路
        vector<bool> visited(n + 1, false);
        vector<int> path;
        path.push_back(s);
        if (!dfs_find_path(s, t, residual, visited, path)) break;   // 没有增广路了 -> 结束

        // 2. 算这条路的瓶颈，也就是这一趟能推多少流
        int bottleneck = bottleneck_of(residual, path);
        if (bottleneck <= 0) break;     // 保险：正常不会发生

        // 3. 沿路更新残留网络：正向边减掉，反向边加上
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            int u = path[i];
            int v = path[i + 1];
            residual[u][v] -= bottleneck;
            residual[v][u] += bottleneck;
        }

        // 4. 记下这一趟，方便做动画和打印过程
        result.value += bottleneck;
        AugmentStep step;
        step.path = path;
        step.bottleneck = bottleneck;
        step.flow_after = result.value;
        result.steps.push_back(step);
    }

    if (result.steps.size() >= SAFETY_LIMIT) {
        cout << "[warn] too many augmenting steps, stopped early" << endl;
    }

    // 每条边实际流了多少 = 原始容量 - 残留容量
    // （两个方向都有边的话，这个值表示"净流量"，可能是负数，画图时只看正的就行）
    for (int u = 1; u <= n; ++u) {
        for (int v = 1; v <= n; ++v) {
            result.flow[u][v] = g.cap_matrix[u][v] - residual[u][v];
        }
    }
    result.residual = residual;

    // 最小割：在最后的残留网络里，从 s 出发能走到的点就是 S 侧，剩下的就是 T 侧
    vector<bool> in_source_side(n + 1, false);
    queue<int> q;
    q.push(s);
    in_source_side[s] = true;

    while (!q.empty()) {
        int u = q.front();
        q.pop();
        for (int v = 1; v <= n; ++v) {
            if (residual[u][v] > 0 && !in_source_side[v]) {
                in_source_side[v] = true;
                q.push(v);
            }
        }
    }

    for (int i = 1; i <= n; ++i) {
        if (in_source_side[i]) result.source_side.push_back(i);
        else result.sink_side.push_back(i);
    }

    // 割边 = 从 S 指向 T 的边
    for (int u = 1; u <= n; ++u) {
        if (!in_source_side[u]) continue;
        for (int v = 1; v <= n; ++v) {
            if (in_source_side[v]) continue;
            if (g.cap_matrix[u][v] <= 0) continue;
            result.cut_edges.push_back(make_pair(u, v));
            result.cut_capacity += g.cap_matrix[u][v];
        }
    }

    return result;
}

void print_flow_result(const Graph& g, const FlowResult& result, int s, int t) {
    cout << "\n--- Max Flow / Min Cut (Ford-Fulkerson) ---" << endl;
    cout << "Source = " << s << ", sink = " << t << endl;

    if (result.steps.empty()) {
        cout << "No augmenting path at all, so max flow = 0." << endl;
    } else {
        cout << "\nAugmenting steps:" << endl;
        for (size_t i = 0; i < result.steps.size(); ++i) {
            const AugmentStep& step = result.steps[i];
            cout << "  " << i + 1 << ") ";
            print_path(step.path);
            cout << "   +" << step.bottleneck << "  (total flow = " << step.flow_after << ")" << endl;
        }
    }

    cout << "\nMax flow value = " << result.value << endl;

    cout << "Min cut: S = { ";
    for (int v : result.source_side) cout << v << " ";
    cout << "}  T = { ";
    for (int v : result.sink_side) cout << v << " ";
    cout << "}" << endl;

    cout << "Cut edges (flow stops here): ";
    if (result.cut_edges.empty()) {
        cout << "(none)";
    } else {
        for (size_t i = 0; i < result.cut_edges.size(); ++i) {
            int u = result.cut_edges[i].first;
            int v = result.cut_edges[i].second;
            cout << u << "->" << v << "(cap " << g.cap_matrix[u][v] << ")";
            if (i + 1 < result.cut_edges.size()) cout << ", ";
        }
    }
    cout << endl;

    cout << "Cut capacity = " << result.cut_capacity
         << "  ->  max flow == min cut ? "
         << (result.cut_capacity == result.value ? "YES (correct)" : "NO (something is wrong!)")
         << endl;

    // 每条边上流了多少，格式是 flow/cap，和窗口里标注的写法一样
    cout << "\nFlow on each edge (flow/cap):" << endl;
    bool any = false;
    for (int u = 1; u <= g.n; ++u) {
        for (int v = 1; v <= g.n; ++v) {
            if (g.cap_matrix[u][v] <= 0) continue;          // 没有这条边
            if (!g.is_directed && v < u) continue;          // 无向图只写一遍
            int f = result.flow[u][v];
            if (f < 0) f = 0;                               // 净流量为负就当 0 显示
            cout << "  " << u << " -> " << v << " : " << f << "/" << g.cap_matrix[u][v] << endl;
            any = true;
        }
    }
    if (!any) cout << "  (no edges)" << endl;
}
