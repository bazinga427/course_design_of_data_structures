// ============================================================
//  graph_io.cpp
//  图结构文件的读写（保存 / 加载）实现。
// ============================================================

#include "graph_io.h"

#include <algorithm>
#include <iterator>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

// 界面上最多画这么多点：再多画起来会卡，而且也不是课程设计要考的东西
const int kMaxNodes = 2000;

// 把一行里的数字全部读出来当参数用。
// 这样一来空格、Tab、逗号、分号 都能当分隔符（"1,2,3" 和 "1 2 3" 一样），
// 行尾以 # / % / // 开头的注释也会被忽略 —— 记事本里随手写的说明
// 不会把整份文件读坏。
std::vector<int> read_numbers(const std::string& line) {
    std::string body = line;
    const size_t hash = body.find_first_of("#%");
    if (hash != std::string::npos) {
        body = body.substr(0, hash);
    }
    const size_t slash = body.find("//");
    if (slash != std::string::npos) {
        body = body.substr(0, slash);
    }

    std::vector<int> values;
    size_t i = 0;
    while (i < body.size()) {
        const bool digit = body[i] >= '0' && body[i] <= '9';
        const bool minus = body[i] == '-' && i + 1 < body.size() &&
                           body[i + 1] >= '0' && body[i + 1] <= '9';
        if (!digit && !minus) {
            ++i;
            continue;
        }

        const bool negative = body[i] == '-';
        if (negative) {
            ++i;
        }
        long value = 0;
        while (i < body.size() && body[i] >= '0' && body[i] <= '9') {
            value = value * 10 + (body[i] - '0');
            if (value > 1000000000L) {
                value = 1000000000L;        // 写得再离谱也不会把这里撑爆
            }
            ++i;
        }
        values.push_back(static_cast<int>(negative ? -value : value));
    }
    return values;
}

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

bool load_graph(Graph& g, const std::string& path, std::string& error, int* base_out) {
    error.clear();
    if (base_out != nullptr) {
        *base_out = 1;
    }

    std::ifstream fin(path.c_str(), std::ios::binary);
    if (!fin) {
        error = "cannot open the file (wrong path, or it is locked by another program)";
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(fin)),
                              std::istreambuf_iterator<char>());
    fin.close();

    std::string body = content;
    // 记事本"另存为 UTF-8"会在文件开头加 BOM，先去干净
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB &&
        static_cast<unsigned char>(body[2]) == 0xBF) {
        body.erase(0, 3);
    }

    struct RawEdge {
        int u;
        int v;
        int cap;
        int line;
    };

    // 先把整个文件读一遍再动手建图：
    // Graph 是直接拿编号当数组下标的，编号一旦越界就会踩内存，
    // 所以必须全部检查通过再建表；顺便把 0 开始编号统一换算掉。
    std::vector<int> header;
    std::vector<RawEdge> raw;
    int header_line = 0;
    int n = 0;
    int m = 0;
    int directed = 1;

    std::istringstream all(body);
    std::string line;
    int line_no = 0;
    while (std::getline(all, line)) {
        ++line_no;
        const std::vector<int> nums = read_numbers(line);
        if (nums.empty()) {
            continue;                           // 空行 / 注释行
        }

        if (header.empty()) {
            header_line = line_no;
            if (nums.size() < 2) {
                error = "line " + std::to_string(header_line) +
                        " should be: n m is_directed";
                return false;
            }
            header = nums;
            n = nums[0];
            m = nums[1];
            // 第三个数不写就默认有向图（和网页版的口径一致）
            directed = nums.size() > 2 ? (nums[2] != 0 ? 1 : 0) : 1;
            continue;
        }

        if (static_cast<int>(raw.size()) >= m) {
            continue;                           // 第一行写了多少条就读多少条
        }
        if (nums.size() < 2) {
            error = "line " + std::to_string(line_no) +
                    ": an edge needs two node ids (u v [capacity])";
            return false;
        }

        RawEdge e;
        e.u = nums[0];
        e.v = nums[1];
        e.cap = nums.size() > 2 ? nums[2] : 1;  // 没写容量就按 1 处理
        if (e.cap <= 0) {
            e.cap = 1;
        }
        e.line = line_no;
        raw.push_back(e);
    }

    if (header.empty()) {
        error = "the first line should be: n m is_directed";
        return false;
    }
    if (n < 1 || n > kMaxNodes) {
        error = "node count n is out of range (this program supports up to 2000)";
        return false;
    }
    if (m < 0) {
        error = "edge count m cannot be negative";
        return false;
    }
    if (static_cast<int>(raw.size()) < m) {
        error = "the file only has " + std::to_string(raw.size()) +
                " edges, but the first line declares " + std::to_string(m);
        return false;
    }

    // 编号基准：只要出现了 0 号点，就当成 0 开始编号（很多教材、以及早前版本的
    // graph.txt 都这么写）。这类文件统一 +1 映射成 1..n，后面所有算法只认 1..n，
    // 就不用每处都判一次"这个图是从 0 还是从 1 开始"。
    bool zero_based = false;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i].u == 0 || raw[i].v == 0) {
            zero_based = true;
        }
    }
    const int lo = zero_based ? 0 : 1;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i].u < lo || raw[i].u > n || raw[i].v < lo || raw[i].v > n) {
            error = "line " + std::to_string(raw[i].line) + ": edge endpoint " +
                    std::to_string(raw[i].u) + " " + std::to_string(raw[i].v) +
                    " is out of range (line " + std::to_string(header_line) +
                    " says n = " + std::to_string(n) + ", so ids should be " +
                    (zero_based ? "0.." : "1..") + std::to_string(n) + ")";
            return false;
        }
    }

    const int shift = zero_based ? 1 : 0;
    int size = n;
    if (zero_based) {
        // 有的 0 开始编号的文件把 n 写成"最大编号"，那点数其实是 n + 1
        for (size_t i = 0; i < raw.size(); ++i) {
            size = std::max(size, std::max(raw[i].u, raw[i].v) + 1);
        }
    }

    // 检查通过，按 1..n 建表：和 Graph::build_from_file 一样是
    // 「邻接表 + 容量矩阵」两套结构一起建好，只是不经过文件
    g.is_directed = directed;
    g.n = size;
    g.m = 0;
    g.adj_list.assign(size + 1, std::vector<int>());
    g.cap_matrix.assign(size + 1, std::vector<int>(size + 1, 0));
    for (size_t i = 0; i < raw.size(); ++i) {
        g.add_edge(raw[i].u + shift, raw[i].v + shift, raw[i].cap);
    }

    if (base_out != nullptr) {
        *base_out = zero_based ? 0 : 1;
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
        error = "cannot write here (read-only file, or no permission)";
        return false;
    }

    fout << n_out << ' ' << edges.size() << ' ' << (g.is_directed ? 1 : 0) << '\n';
    for (const EdgeWithCap& e : edges) {
        // 容量写在第三个位置；不写的话读回来就全变成默认的 1，最大流会算错
        fout << e.u << ' ' << e.v << ' ' << e.capacity << '\n';
    }
    fout.close();

    if (!fout) {
        error = "error while writing the file";
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
