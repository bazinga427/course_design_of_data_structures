// ============================================================
//  graph_io.cpp
//  图结构文件的读写（保存 / 加载）实现。
// ============================================================

#include "graph_io.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

// 界面上最多画这么多点：再多画起来会卡，而且也不是课程设计要考的东西
const int kMaxNodes = 2000;

}  // namespace

namespace graph_io {

// 一条边 + 它的容量。
// 容量是给最大流用的：不带容量的图，统一按 1 处理。
struct EdgeWithCap {
    int u;
    int v;
    int capacity;
};

// 图里所有的边（去重）以及每条边的容量。无向图只保留 u<=v 的一份。
std::vector<EdgeWithCap> edges_with_capacity(const Graph& g) {
    std::vector<EdgeWithCap> edges;
    std::set<std::pair<int, int>> seen;

    for (int u = 1; u < static_cast<int>(g.adj_list.size()); ++u) {
        for (int v : g.adj_list[u]) {
            if (u < 1 || v < 1) {
                continue;
            }
            // 无向图：u-v 和 v-u 是同一条边，统一成 (小的, 大的) 再判重
            std::pair<int, int> key = (!g.is_directed && v < u)
                                          ? std::make_pair(v, u)
                                          : std::make_pair(u, v);
            if (!seen.insert(key).second) {
                continue;
            }

            int cap = 1;
            if (u < static_cast<int>(g.cap_matrix.size()) &&
                v < static_cast<int>(g.cap_matrix[u].size())) {
                cap = g.cap_matrix[u][v];
            }
            if (cap <= 0) {
                cap = 1;
            }

            EdgeWithCap e;
            e.u = key.first;
            e.v = key.second;
            e.capacity = cap;
            edges.push_back(e);
        }
    }

    std::sort(edges.begin(), edges.end(),
              [](const EdgeWithCap& a, const EdgeWithCap& b) {
                  if (a.u != b.u) return a.u < b.u;
                  return a.v < b.v;
              });
    return edges;
}

bool is_zero_based(const Graph& g) {
    if (g.n <= 0 || g.adj_list.empty()) {
        return false;
    }

    // 0 号点自己有出边
    if (0 < static_cast<int>(g.adj_list.size()) && !g.adj_list[0].empty()) {
        return true;
    }

    // 0 号点只有入边（别的点指向它）
    for (int u = 0; u < static_cast<int>(g.adj_list.size()); ++u) {
        for (int v : g.adj_list[u]) {
            if (v == 0) {
                return true;
            }
        }
    }

    return false;
}

std::vector<int> node_ids(const Graph& g) {
    const int limit = std::max(0, g.n);
    std::vector<bool> mark(static_cast<size_t>(limit) + 2, false);

    // 先把边表里出现过的编号全部标上
    for (int u = 0; u < static_cast<int>(g.adj_list.size()); ++u) {
        for (int v : g.adj_list[u]) {
            if (u >= 0 && u < static_cast<int>(mark.size())) {
                mark[u] = true;
            }
            if (v >= 0 && v < static_cast<int>(mark.size())) {
                mark[v] = true;
            }
        }
    }

    // 再把「没有边的孤立点」补上：0 开始编号就是 0..n-1，1 开始编号就是 1..n
    const bool zero_based = is_zero_based(g);
    const int lo = zero_based ? 0 : 1;
    int hi = zero_based ? limit - 1 : limit;
    hi = std::min(hi, static_cast<int>(mark.size()) - 1);
    for (int i = lo; i <= hi; ++i) {
        mark[i] = true;
    }

    std::vector<int> ids;
    for (int i = 0; i < static_cast<int>(mark.size()); ++i) {
        if (mark[i]) {
            ids.push_back(i);
        }
    }
    return ids;
}

std::vector<std::pair<int, int>> edge_list(const Graph& g) {
    std::vector<std::pair<int, int>> edges;
    for (const EdgeWithCap& e : edges_with_capacity(g)) {
        edges.push_back(std::make_pair(e.u, e.v));
    }
    return edges;
}

bool load_graph(Graph& g, const std::string& path, std::string& error) {
    error.clear();

    std::ifstream fin(path.c_str());
    if (!fin) {
        error = "打不开文件（路径不对，或者文件被别的程序占用了）";
        return false;
    }

    int n = 0, m = 0, directed = 0;
    if (!(fin >> n >> m >> directed)) {
        error = "文件第一行应该是：n m is_directed";
        return false;
    }
    if (n < 0 || n > kMaxNodes) {
        error = "节点数 n 超出范围（本程序最大支持 2000）";
        return false;
    }
    if (m < 0) {
        error = "边数 m 不能是负数";
        return false;
    }

    // 先把边全部读出来检查一遍：
    // Graph::build_from_file 是直接拿编号当数组下标的，编号一旦越界就会踩内存，
    // 所以这里必须先拦住，顺便检查文件里声明的边数够不够。
    //
    // 每行是 u v [容量]。这里必须一行一行读：只按整数读两个的话，
    // 第三个数字（容量）会被当成下一行的节点编号，整份文件就读偏了
    // （容量大于 n 时会报出「边的端点 100 1 超出范围」这种莫名其妙的错）。
    std::vector<std::pair<int, int>> edges;
    edges.reserve(static_cast<size_t>(m));

    std::string line;
    std::getline(fin, line);                    // 吃掉第一行剩下的换行
    while (static_cast<int>(edges.size()) < m && std::getline(fin, line)) {
        std::istringstream ss(line);
        int u = 0, v = 0, cap = 1;
        if (!(ss >> u >> v)) {
            continue;                           // 空行之类的直接跳过
        }
        ss >> cap;                              // 没写容量就按 1 处理

        if (u < 1 || u > n || v < 1 || v > n) {
            error = "边的端点 " + std::to_string(u) + " " + std::to_string(v) +
                    " 超出范围（节点编号是 1.." + std::to_string(n) + "，从 1 开始）";
            return false;
        }
        edges.push_back(std::make_pair(u, v));
    }
    if (static_cast<int>(edges.size()) < m) {
        error = "文件里只有 " + std::to_string(edges.size()) + " 条边，但第一行声明了 " +
                std::to_string(m) + " 条";
        return false;
    }
    fin.close();

    // 检查通过，交给队友写的读取函数：它会同时建好邻接表和邻接矩阵
    g.build_from_file(path);
    if (g.n != n) {
        error = "读取结果和文件头对不上，请检查文件";
        return false;
    }
    return true;
}

bool save_graph(const Graph& g, const std::string& path, std::string& error) {
    error.clear();

    const std::vector<int> ids = node_ids(g);
    const std::vector<EdgeWithCap> edges = edges_with_capacity(g);

    // n 按「编号最大值」写：1 开始编号时 n 就是最大编号，
    // 0 开始编号时节点数是最大编号 + 1
    int n_out = 0;
    if (!ids.empty()) {
        n_out = is_zero_based(g) ? ids.back() + 1 : ids.back();
    }

    std::ofstream fout(path.c_str());
    if (!fout) {
        error = "写不进这个位置（可能是只读文件，或者没有权限）";
        return false;
    }

    fout << n_out << ' ' << edges.size() << ' ' << (g.is_directed ? 1 : 0) << '\n';
    for (const EdgeWithCap& e : edges) {
        // 容量写在第三个位置；不写的话读回来就全变成默认的 1，最大流会算错
        fout << e.u << ' ' << e.v << ' ' << e.capacity << '\n';
    }
    fout.close();

    if (!fout) {
        error = "写文件的时候出错了";
        return false;
    }
    return true;
}

std::string layout_path_of(const std::string& graph_path) {
    return graph_path + ".layout";
}

bool load_layout(const std::string& graph_path, std::vector<NodePos>& pos) {
    std::ifstream fin(layout_path_of(graph_path).c_str());
    if (!fin) {
        return false;
    }

    int count = 0;
    if (!(fin >> count) || count < 0) {
        return false;
    }

    std::unordered_map<int, std::pair<double, double>> table;
    for (int i = 0; i < count; ++i) {
        int id = 0;
        double x = 0.0, y = 0.0;
        if (!(fin >> id >> x >> y)) {
            return false;
        }
        table[id] = std::make_pair(x, y);
    }

    // 每个节点都要有坐标才算能用，缺一个就整体作废（重新布局）
    for (NodePos& p : pos) {
        const auto it = table.find(p.id);
        if (it == table.end()) {
            return false;
        }
        p.x = it->second.first;
        p.y = it->second.second;
    }
    return true;
}

bool save_layout(const std::string& graph_path, const std::vector<NodePos>& pos) {
    std::ofstream fout(layout_path_of(graph_path).c_str());
    if (!fout) {
        return false;
    }
    // 坐标是 double，默认只写 6 位有效数字，读回来会有误差，这里多留几位
    fout << std::setprecision(12);
    fout << pos.size() << '\n';
    for (const NodePos& p : pos) {
        fout << p.id << ' ' << p.x << ' ' << p.y << '\n';
    }
    fout.close();
    return static_cast<bool>(fout);
}

}  // namespace graph_io
