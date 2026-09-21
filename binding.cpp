// ============================================================
//  binding.cpp  ——  C++ 核心 与 前端(JavaScript) 之间的适配层
//
//  前端只认识下面这几个函数，它们统一返回一段 JSON 文本（UTF-8）：
//
//    gv_version()                             版本号 / 自检
//    gv_load_text(text, fileName)             用文件内容建图（“读取文件恢复图”）
//    gv_random(n, m, directed, seed, maxCap)  随机生成图（“创建随机图文件”）
//    gv_save_text()                           把当前图导出成文件文本（保存 / 下载）
//    gv_snapshot()                            邻接表 + 邻接矩阵 + 边表
//    gv_traverse(mode, start)                 DFS / BFS 遍历序列 + 每个点的父节点
//    gv_check_sequence(start, seqText)        遍历序列合法性判别
//    gv_max_flow(s, t)                        最大流 + 每一次增广 + 最小割
//
//  返回值里的 JSON 结构在 FRONTEND.md 里写清楚了，
//  前端 src/wasm/loader.js 就是按这份契约调用的。
//
//  两点说明：
//   1) 这里没有任何算法，算法全部复用成员 B 的 graph.cpp / flow.cpp / graph_io.cpp，
//      这个文件只做“参数搬运 + JSON 拼装”，保证网页和 EasyX 版本算出来一模一样。
//   2) 图上所有函数都以 1..n 编号，和 C++ 核心一致。
//      gv_load_text 的返回值里多一个 "base" 字段：1 = 文件本来就是 1 开始编号，
//      0 = 文件是 0 开始编号、已经自动 +1 映射过（前端提示一句，见 graph_io.cpp）。
//
//  编译：VSCode 里 Ctrl+Shift+B 选「编译 WASM」，或者双击 tools/build_wasm.bat
// ============================================================

#include "graph.h"
#include "flow.h"
#include "graph_io.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define GV_API extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define GV_API extern "C"
#endif

namespace {

// 浏览器里同时只会操作一张图，所以这里就放一张全局图，不用句柄池。
Graph g_graph(true);

// 返回给 JS 的 JSON 缓冲。JS 端会在调用返回时立刻把内容复制走，
// 所以只要在这次调用期间这个字符串不被改写就是安全的。
std::string g_json;

// 邻接矩阵太大就别塞进 JSON 了（n = 2000 时矩阵有 400 万个数字）。
const int kMatrixLimit = 150;
// 超过这个点数就只做计算、不做花里胡哨的渲染，避免浏览器卡住。
const int kMaxNodes = 2000;

const char* keep(const std::string& text) {
    g_json = text;
    return g_json.c_str();
}

void jstr(std::string& out, const std::string& s) {
    out += '"';
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out += static_cast<char>(ch);
                }
        }
    }
    out += '"';
}

void jints(std::string& out, const std::vector<int>& values) {
    out += '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(values[i]);
    }
    out += ']';
}

void jbool(std::string& out, bool value) { out += value ? "true" : "false"; }

std::string error_json(const std::string& message) {
    std::string out = "{\"ok\":false,\"error\":";
    jstr(out, message);
    out += '}';
    return out;
}

// base = 1：文件里的编号就是 1..n；base = 0：文件用的是 0 开始编号，
// graph_io::load_graph 已经把它 +1 映射成 1..n 了（前端会提示一句）。
std::string info_json(const std::string& file_name, int base) {
    std::string out = "{\"ok\":true,\"fileName\":";
    jstr(out, file_name);
    out += ",\"n\":" + std::to_string(g_graph.n);
    out += ",\"m\":" + std::to_string(g_graph.m);
    out += ",\"directed\":" + std::string(g_graph.is_directed ? "1" : "0");
    out += ",\"base\":" + std::to_string(base);
    out += '}';
    return out;
}

// 一条边 + 它的容量。无向图只保留 u <= v 的一份，和 graph_io.cpp 的口径一致。
struct GVEdge {
    int u;
    int v;
    int cap;
};

std::vector<GVEdge> collect_edges(const Graph& g) {
    std::vector<GVEdge> edges;

    for (int u = 1; u <= g.n; ++u) {
        if (u >= static_cast<int>(g.adj_list.size())) break;
        for (size_t k = 0; k < g.adj_list[u].size(); ++k) {
            int v = g.adj_list[u][k];
            if (v < 1 || v > g.n) continue;
            if (!g.is_directed && v < u) continue;  // 无向图两个方向存了两遍，只留一遍

            int cap = 1;
            if (u < static_cast<int>(g.cap_matrix.size()) &&
                v < static_cast<int>(g.cap_matrix[u].size())) {
                cap = g.cap_matrix[u][v];
            }
            if (cap <= 0) cap = 1;

            GVEdge e;
            e.u = u;
            e.v = v;
            e.cap = cap;
            edges.push_back(e);
        }
    }

    std::sort(edges.begin(), edges.end(), [](const GVEdge& a, const GVEdge& b) {
        if (a.u != b.u) return a.u < b.u;
        return a.v < b.v;
    });
    // 邻接表里同一对点可能被 add_edge 插了两次，这里去重
    edges.erase(std::unique(edges.begin(), edges.end(),
                            [](const GVEdge& a, const GVEdge& b) {
                                return a.u == b.u && a.v == b.v;
                            }),
                edges.end());
    return edges;
}

std::vector<int> parse_int_list(const std::string& text) {
    std::vector<int> values;
    std::istringstream ss(text);
    int value = 0;
    while (ss >> value) values.push_back(value);
    return values;
}

}  // namespace

// ------------------------------------------------------------
//  自检 / 版本
// ------------------------------------------------------------
GV_API const char* gv_version() {
    std::string out = "{\"ok\":true,\"name\":\"graph_core\",\"version\":\"1.0.0\",\"maxNodes\":";
    out += std::to_string(kMaxNodes);
    out += ",\"matrixLimit\":" + std::to_string(kMatrixLimit);
    out += '}';
    return keep(out);
}

// ------------------------------------------------------------
//  建图：读文件内容 / 随机生成
// ------------------------------------------------------------
GV_API const char* gv_load_text(const char* text, const char* file_name) {
    const std::string body = text != nullptr ? std::string(text) : std::string();
    if (body.empty()) return keep(error_json("文件是空的，没有内容可以解析"));

    // graph_io::load_graph 是从文件读的，所以先把内容写进临时文件
    //（WASM 里这就是 Emscripten 的虚拟文件系统，路径用相对路径最稳）。
    const std::string path = "gv_io_input.tmp";
    {
        std::ofstream out(path.c_str(), std::ios::binary);
        if (!out) return keep(error_json("无法写临时文件，请检查运行目录的写权限"));
        out << body;
        out.close();
    }

    Graph fresh(true);
    std::string error;
    int base = 1;
    if (!graph_io::load_graph(fresh, path, error, &base)) {
        return keep(error_json(error));
    }

    g_graph = fresh;
    return keep(info_json(file_name != nullptr ? std::string(file_name) : std::string(), base));
}

GV_API const char* gv_random(int n, int m, int directed, int seed, int max_capacity) {
    if (n < 1 || n > kMaxNodes) {
        return keep(error_json("节点数必须在 1.." + std::to_string(kMaxNodes) + " 之间"));
    }
    if (m < 0) return keep(error_json("边数不能是负数"));
    if (max_capacity < 1) max_capacity = 1;

    // 无自环时边数上限：有向 n*(n-1)，无向 n*(n-1)/2
    const int limit = directed != 0 ? n * (n - 1) : n * (n - 1) / 2;
    if (m > limit) m = limit;

    Graph fresh(directed != 0);
    fresh.build_random(n, m, directed != 0,
                       seed > 0 ? static_cast<unsigned>(seed) : 0u, max_capacity);
    g_graph = fresh;

    std::string out = "{\"ok\":true,\"n\":" + std::to_string(fresh.n);
    out += ",\"m\":" + std::to_string(fresh.m);
    out += ",\"directed\":" + std::string(fresh.is_directed ? "1" : "0");
    out += ",\"seed\":" + std::to_string(seed);
    out += ",\"maxCapacity\":" + std::to_string(max_capacity);
    out += '}';
    return keep(out);
}

// ------------------------------------------------------------
//  保存：导出成和 graph.txt 一样的文件文本
// ------------------------------------------------------------
GV_API const char* gv_save_text() {
    const std::string path = "gv_io_output.tmp";
    std::string error;
    if (!graph_io::save_graph(g_graph, path, error)) {
        return keep(error_json(error.empty() ? "导出失败" : error));
    }

    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return keep(error_json("导出的文件读不回来"));
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();

    std::string out = "{\"ok\":true,\"text\":";
    jstr(out, buffer.str());
    out += '}';
    return keep(out);
}

// ------------------------------------------------------------
//  当前这张图长什么样：节点 / 边 / 邻接表 / 邻接矩阵
// ------------------------------------------------------------
GV_API const char* gv_snapshot() {
    const Graph& g = g_graph;
    const std::vector<int> ids = graph_io::node_ids(g);
    const std::vector<GVEdge> edges = collect_edges(g);

    std::string out = "{\"ok\":true,\"n\":" + std::to_string(g.n);
    out += ",\"m\":" + std::to_string(edges.size());
    out += ",\"directed\":" + std::string(g.is_directed ? "1" : "0");

    out += ",\"nodes\":";
    jints(out, ids);

    // 边表：前端画图、算流量标签都用它
    out += ",\"edges\":[";
    for (size_t i = 0; i < edges.size(); ++i) {
        if (i) out += ',';
        out += "{\"u\":" + std::to_string(edges[i].u);
        out += ",\"v\":" + std::to_string(edges[i].v);
        out += ",\"cap\":" + std::to_string(edges[i].cap);
        out += '}';
    }
    out += ']';

    // 邻接表：下标 0 空着不用，adj[u] 就是 u 的所有邻居（顺序和算法里完全一致）
    out += ",\"adjList\":[";
    for (int u = 0; u <= g.n; ++u) {
        if (u) out += ',';
        if (u < static_cast<int>(g.adj_list.size())) {
            jints(out, g.adj_list[u]);
        } else {
            out += "[]";
        }
    }
    out += ']';

    // 邻接矩阵 / 容量矩阵：[u][v] 就是 u->v 的容量，0 表示没有这条边
    const bool show_matrix = g.n <= kMatrixLimit;
    out += ",\"matrixOmitted\":";
    jbool(out, !show_matrix);
    out += ",\"matrix\":";
    if (!show_matrix) {
        out += "null";
    } else {
        out += '[';
        for (int u = 0; u <= g.n; ++u) {
            if (u) out += ',';
            out += '[';
            for (int v = 0; v <= g.n; ++v) {
                if (v) out += ',';
                int cap = 0;
                if (u < static_cast<int>(g.cap_matrix.size()) &&
                    v < static_cast<int>(g.cap_matrix[u].size())) {
                    cap = g.cap_matrix[u][v];
                }
                out += std::to_string(cap);
            }
            out += ']';
        }
        out += ']';
    }

    out += '}';
    return keep(out);
}

// ------------------------------------------------------------
//  DFS / BFS 遍历
//    mode = 0 -> DFS, mode = 1 -> BFS
// ------------------------------------------------------------
GV_API const char* gv_traverse(int mode, int start) {
    const Graph& g = g_graph;

    if (start < 1 || start > g.n) {
        return keep(error_json("起点必须在 1.." + std::to_string(g.n) + " 之间"));
    }

    std::vector<int> parent;
    std::vector<int> seq;
    if (mode == 1) {
        seq = g.get_bfs_sequence(start, &parent);
    } else {
        seq = g.get_dfs_sequence(start, &parent);
    }

    std::string out = "{\"ok\":true,\"mode\":" + std::to_string(mode == 1 ? 1 : 0);
    out += ",\"start\":" + std::to_string(start);
    out += ",\"seq\":";
    jints(out, seq);
    out += ",\"reachable\":";
    jints(out, g.reachable_from(start));

    // 走下来的“树边” (父, 子)，动画里高亮它们
    out += ",\"parentEdges\":[";
    bool first = true;
    for (int v = 1; v <= g.n && v < static_cast<int>(parent.size()); ++v) {
        if (parent[v] == 0) continue;
        if (!first) out += ',';
        first = false;
        out += '[' + std::to_string(parent[v]) + ',' + std::to_string(v) + ']';
    }
    out += ']';

    out += '}';
    return keep(out);
}

// ------------------------------------------------------------
//  遍历序列合法性判别
// ------------------------------------------------------------
GV_API const char* gv_check_sequence(int start, const char* seq_text) {
    const Graph& g = g_graph;
    if (start < 1 || start > g.n) {
        return keep(error_json("起点必须在 1.." + std::to_string(g.n) + " 之间"));
    }

    const std::vector<int> seq = parse_int_list(seq_text != nullptr ? std::string(seq_text) : "");
    if (seq.empty()) return keep(error_json("请先输入要判别的序列，例如：1 2 3 4"));

    const bool dfs_ok = g.is_valid_dfs(seq, start);
    const bool bfs_ok = g.is_valid_bfs(seq, start);

    std::string out = "{\"ok\":true,\"start\":" + std::to_string(start);
    out += ",\"seq\":";
    jints(out, seq);
    out += ",\"dfs\":";
    jbool(out, dfs_ok);
    out += ",\"bfs\":";
    jbool(out, bfs_ok);
    out += ",\"sampleDfs\":";
    jints(out, g.get_dfs_sequence(start));
    out += ",\"sampleBfs\":";
    jints(out, g.get_bfs_sequence(start));
    out += '}';
    return keep(out);
}

// ------------------------------------------------------------
//  最大流 / 最小割（Ford-Fulkerson）
// ------------------------------------------------------------
GV_API const char* gv_max_flow(int s, int t) {
    const Graph& g = g_graph;

    if (g.n < 2) return keep(error_json("至少要有两个点才能算最大流"));
    if (s < 1 || s > g.n || t < 1 || t > g.n) {
        return keep(error_json("源点和汇点都必须在 1.." + std::to_string(g.n) + " 之间"));
    }
    if (s == t) return keep(error_json("源点和汇点不能是同一个点"));

    const FlowResult r = max_flow_min_cut(g, s, t);

    std::string out = "{\"ok\":true,\"s\":" + std::to_string(s);
    out += ",\"t\":" + std::to_string(t);
    out += ",\"value\":" + std::to_string(r.value);
    out += ",\"cutCapacity\":" + std::to_string(r.cut_capacity);
    out += ",\"correct\":";
    jbool(out, r.cut_capacity == r.value);

    // 每一次增广：路径 + 这条路的瓶颈 + 推完以后的总流量
    out += ",\"steps\":[";
    for (size_t i = 0; i < r.steps.size(); ++i) {
        if (i) out += ',';
        out += "{\"path\":";
        jints(out, r.steps[i].path);
        out += ",\"bottleneck\":" + std::to_string(r.steps[i].bottleneck);
        out += ",\"flowAfter\":" + std::to_string(r.steps[i].flow_after);
        out += '}';
    }
    out += ']';

    out += ",\"sourceSide\":";
    jints(out, r.source_side);
    out += ",\"sinkSide\":";
    jints(out, r.sink_side);

    out += ",\"cutEdges\":[";
    for (size_t i = 0; i < r.cut_edges.size(); ++i) {
        if (i) out += ',';
        out += '[' + std::to_string(r.cut_edges[i].first) + ',' +
               std::to_string(r.cut_edges[i].second) + ']';
    }
    out += ']';

    // 每条边的容量 / 实际流量。
    // flow 是“净流量”，可能有负值（无向图会出现），前端显示时按 0 处理，
    // 和 EasyX 窗口里 print_flow_result 的口径保持一致。
    const std::vector<GVEdge> edges = collect_edges(g);
    out += ",\"edgeFlow\":[";
    for (size_t i = 0; i < edges.size(); ++i) {
        if (i) out += ',';
        const int u = edges[i].u;
        const int v = edges[i].v;
        const int cap = g.cap_matrix[u][v];
        const int flow = r.flow[u][v];
        out += "{\"u\":" + std::to_string(u);
        out += ",\"v\":" + std::to_string(v);
        out += ",\"cap\":" + std::to_string(cap);
        out += ",\"flow\":" + std::to_string(flow);
        out += '}';
    }
    out += ']';

    out += '}';
    return keep(out);
}
