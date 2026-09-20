#include "graph.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <sstream>
#include <utility>

using namespace std;

// 控制台输出一律用英文：Windows 控制台默认是 GBK 编码，
// 直接 cout 中文会变成乱码，所以代码里的注释用中文、输出用英文。

Graph::Graph(bool directed) {
    is_directed = directed ? 1 : 0;
    n = 0;
    m = 0;
}

// ------------------------------------------------------------
// 建图
// ------------------------------------------------------------

bool Graph::build_from_file(const string& filename) {
    ifstream fin(filename);
    if (!fin) {
        cout << "[error] cannot open file: " << filename << endl;
        return false;
    }

    string line;
    if (!getline(fin, line)) {
        cout << "[error] empty file: " << filename << endl;
        return false;
    }

    // 第一行：n m is_directed（is_directed 可以不写，那就保持构造函数里的设置）
    istringstream head(line);
    int edge_count = 0;
    int directed_flag = is_directed;
    if (!(head >> n >> edge_count)) {
        cout << "[error] bad first line, expected: n m is_directed" << endl;
        return false;
    }
    head >> directed_flag;              // 读不到就还是原来的值
    is_directed = directed_flag ? 1 : 0;

    if (n <= 0) {
        cout << "[error] n must be positive" << endl;
        return false;
    }

    adj_list.assign(n + 1, vector<int>());
    cap_matrix.assign(n + 1, vector<int>(n + 1, 0));
    m = 0;

    // 一行一行读，这样写不写容量都能兼容
    int read_edges = 0;
    while (read_edges < edge_count && getline(fin, line)) {
        istringstream ss(line);
        int u = 0, v = 0, capacity = 1;
        if (!(ss >> u >> v)) continue;  // 空行之类的直接跳过
        ss >> capacity;                 // 没写容量就默认 1

        if (u < 1 || u > n || v < 1 || v > n) {
            cout << "[warn] edge " << u << " " << v << " out of range 1.." << n
                 << ", skipped" << endl;
            continue;
        }
        if (capacity <= 0) capacity = 1;

        add_edge(u, v, capacity);
        ++read_edges;
    }

    cout << "Graph loaded from " << filename << "  (N = " << n << ", M = " << m
         << ", " << (is_directed ? "directed" : "undirected") << ")" << endl;
    return true;
}

void Graph::build_random(int node_count, int edge_count, bool directed, unsigned seed) {
    is_directed = directed ? 1 : 0;
    n = node_count;

    if (n < 1) n = 1;                   // 至少得有一个点，不然下面没法分配空间
    adj_list.assign(n + 1, vector<int>());
    cap_matrix.assign(n + 1, vector<int>(n + 1, 0));
    m = 0;

    // 不允许自环（自己连自己），所以最多只能有这么多条边：
    //   有向图 n*(n-1)，无向图 n*(n-1)/2
    int max_edges = directed ? n * (n - 1) : n * (n - 1) / 2;
    if (edge_count > max_edges) {
        cout << "[warn] a " << (directed ? "directed" : "undirected") << " graph with "
             << n << " nodes has at most " << max_edges << " edges, using that instead" << endl;
        edge_count = max_edges;
    }
    if (edge_count < 0) edge_count = 0;

    // seed == 0 用真随机数做种子（每次都不一样），
    // 否则用指定的种子（同一个种子每次生成的图完全一样，写报告的时候很有用）
    unsigned used_seed = (seed == 0) ? random_device{}() : seed;
    mt19937 rng(used_seed);
    uniform_int_distribution<int> pick(1, n);

    if (n >= 2 && edge_count > 0) {
        if (edge_count > max_edges / 2) {
            // 稠密图：把所有可能的边列出来打乱，取前 edge_count 条。
            // 边多的时候靠随机抽会一直抽到重复的边，非常慢。
            vector<pair<int, int>> all_edges;
            all_edges.reserve(max_edges);
            for (int u = 1; u <= n; ++u) {
                for (int v = 1; v <= n; ++v) {
                    if (u == v) continue;                   // 跳过自环
                    if (!directed && v < u) continue;       // 无向图只留一半，避免重复
                    all_edges.push_back(make_pair(u, v));
                }
            }
            shuffle(all_edges.begin(), all_edges.end(), rng);

            for (int i = 0; i < edge_count; ++i) {
                add_edge(all_edges[i].first, all_edges[i].second, 1);
            }
        } else {
            // 稀疏图：随机抽两个点当一条边，抽重了就再来一次
            while (m < edge_count) {
                int u = pick(rng);
                int v = pick(rng);

                if (u == v) continue;                       // 不要自环
                if (cap_matrix[u][v] != 0) continue;        // 这条边已经有了
                if (!directed && cap_matrix[v][u] != 0) continue;

                add_edge(u, v, 1);
            }
        }
    }

    cout << "Random graph generated  (N = " << n << ", M = " << m << ", "
         << (is_directed ? "directed" : "undirected")
         << ", seed = " << used_seed << ")" << endl;
    cout << "  tip: use seed = " << used_seed << " to get this exact graph again" << endl;
}

void Graph::add_edge(int u, int v, int capacity) {
    if (u < 1 || u > n || v < 1 || v > n) {
        cout << "[warn] add_edge(" << u << ", " << v << ") out of range 1.." << n
             << ", ignored" << endl;
        return;
    }

    adj_list[u].push_back(v);
    cap_matrix[u][v] = capacity;
    ++m;

    if (!is_directed) {                 // 无向图两个方向都要存
        adj_list[v].push_back(u);
        cap_matrix[v][u] = capacity;
    }
}

bool Graph::save_to_file(const string& filename) const {
    ofstream fout(filename);
    if (!fout) {
        cout << "[error] cannot write file: " << filename << endl;
        return false;
    }

    fout << n << " " << m << " " << is_directed << "\n";
    for (int u = 1; u <= n; ++u) {
        for (int v = 1; v <= n; ++v) {
            if (cap_matrix[u][v] == 0) continue;
            if (!is_directed && v < u) continue;        // 无向图只写一遍
            fout << u << " " << v << " " << cap_matrix[u][v] << "\n";
        }
    }

    cout << "Graph saved to " << filename << endl;
    return true;
}

// ------------------------------------------------------------
// 遍历
// ------------------------------------------------------------

void Graph::dfs_util(int u, vector<bool>& visited, vector<int>& seq) const {
    visited[u] = true;
    seq.push_back(u);

    for (int v : adj_list[u]) {
        if (!visited[v]) {
            dfs_util(v, visited, seq);
        }
    }
}

vector<int> Graph::get_dfs_sequence(int start_node) const {
    if (start_node < 1 || start_node > n) {
        cout << "[warn] start node " << start_node << " out of range 1.." << n << endl;
        return {};
    }

    vector<bool> visited(n + 1, false);
    vector<int> seq;
    dfs_util(start_node, visited, seq);
    return seq;
}

vector<int> Graph::get_bfs_sequence(int start_node) const {
    if (start_node < 1 || start_node > n) {
        cout << "[warn] start node " << start_node << " out of range 1.." << n << endl;
        return {};
    }

    vector<bool> visited(n + 1, false);
    queue<int> q;
    vector<int> seq;

    q.push(start_node);
    visited[start_node] = true;

    while (!q.empty()) {
        int u = q.front();
        q.pop();
        seq.push_back(u);

        for (int v : adj_list[u]) {
            if (!visited[v]) {
                visited[v] = true;
                q.push(v);
            }
        }
    }
    return seq;
}

vector<int> Graph::reachable_from(int start_node) const {
    vector<int> result;
    if (start_node < 1 || start_node > n) return result;

    vector<bool> visited(n + 1, false);
    queue<int> q;
    q.push(start_node);
    visited[start_node] = true;

    while (!q.empty()) {
        int u = q.front();
        q.pop();
        result.push_back(u);

        for (int v : adj_list[u]) {
            if (!visited[v]) {
                visited[v] = true;
                q.push(v);
            }
        }
    }

    sort(result.begin(), result.end());
    return result;
}

// ------------------------------------------------------------
// 遍历序列判别
//
// 思路：不比对"标准答案"，而是看这个序列真的能不能走出来。
//
// DFS：一开始栈里只有起点。序列的下一个点必须是栈顶的未访问邻居
//      （DFS 只要还有没访问的邻居，就一定会继续往下走）；
//      栈顶要是没有未访问的邻居，就说明这里该返回了，弹掉再试。
//      最后序列必须覆盖整个连通块。
//
// BFS：算出序列里每个点的层数（层数 = 前面最早出现的邻居的层数加一），
//      要求层数沿着序列不减；并且同一层的点，它们的父节点在序列里的
//      位置也必须不减（队列先进先出，先处理的父亲，孩子一定先出队）。
// ------------------------------------------------------------

bool Graph::has_unvisited_neighbor(int u, const vector<bool>& visited) const {
    for (int v : adj_list[u]) {
        if (!visited[v]) return true;
    }
    return false;
}

bool Graph::is_valid_dfs(const vector<int>& seq, int start_node) const {
    if (seq.empty() || seq[0] != start_node) return false;
    if (start_node < 1 || start_node > n) return false;

    vector<bool> visited(n + 1, false);
    vector<int> stack;
    visited[start_node] = true;
    stack.push_back(start_node);

    for (size_t i = 1; i < seq.size(); ++i) {
        int v = seq[i];
        if (v < 1 || v > n || visited[v]) return false;     // 越界或者重复访问

        // 栈顶没有未访问的邻居就说明它该返回了，弹掉继续看上一层
        while (!stack.empty() && !has_unvisited_neighbor(stack.back(), visited)) {
            stack.pop_back();
        }
        if (stack.empty()) return false;                    // 已经走完了，不该再有新点
        if (cap_matrix[stack.back()][v] == 0) return false; // 只能从栈顶往下走

        visited[v] = true;
        stack.push_back(v);
    }

    // DFS 会把能走到的点全走一遍，所以长度必须等于连通块大小
    return seq.size() == reachable_from(start_node).size();
}

bool Graph::is_valid_bfs(const vector<int>& seq, int start_node) const {
    if (seq.empty() || seq[0] != start_node) return false;
    if (start_node < 1 || start_node > n) return false;

    vector<int> level(n + 1, -1);       // -1 表示还没出现过
    vector<int> parent_pos(n + 1, -1);// 父节点在序列里的下标
    level[start_node] = 0;

    for (size_t i = 1; i < seq.size(); ++i) {
        int v = seq[i];
        if (v < 1 || v > n || level[v] != -1) return false; // 越界或者重复出现

        // 找它前面最早出现的邻居，那个点就是它在 BFS 树里的父亲
        int best_level = -1;
        int best_pos = -1;
        for (size_t j = 0; j < i; ++j) {
            int u = seq[j];
            if (cap_matrix[u][v] == 0) continue;
            if (best_pos == -1 || level[u] < best_level) {
                best_level = level[u];
                best_pos = static_cast<int>(j);
            }
        }
        if (best_pos == -1) return false;                   // 和前面的点都不相邻，接不上

        level[v] = best_level + 1;
        parent_pos[v] = best_pos;

        // 层数必须不减（BFS 是一层一层往外走的）
        if (level[v] < level[seq[i - 1]]) return false;

        // 同一层的点，父亲的出场顺序也必须不减
        for (size_t j = 1; j < i; ++j) {
            int u = seq[j];
            if (level[u] == level[v] && parent_pos[u] > parent_pos[v]) return false;
        }
    }

    return seq.size() == reachable_from(start_node).size();
}

void Graph::check_sequence(const vector<int>& user_seq, int start_node) const {
    if (user_seq.empty()) {
        cout << "Result: empty sequence, nothing to check." << endl;
        return;
    }

    bool dfs_ok = is_valid_dfs(user_seq, start_node);
    bool bfs_ok = is_valid_bfs(user_seq, start_node);

    cout << "Start node: " << start_node << ", sequence length: " << user_seq.size() << endl;
    if (dfs_ok && bfs_ok) {
        cout << "Result: a valid DFS order, and also a valid BFS order" << endl;
    } else if (dfs_ok) {
        cout << "Result: a valid DFS order" << endl;
    } else if (bfs_ok) {
        cout << "Result: a valid BFS order" << endl;
    } else {
        cout << "Result: not a valid DFS or BFS order for this graph" << endl;
    }

    // 把标准答案也打出来方便对照（注意合法答案不止这一个）
    cout << "  one possible DFS: ";
    for (int v : get_dfs_sequence(start_node)) cout << v << " ";
    cout << endl;
    cout << "  one possible BFS: ";
    for (int v : get_bfs_sequence(start_node)) cout << v << " ";
    cout << endl;
}

// ------------------------------------------------------------
// 输出
// ------------------------------------------------------------

void Graph::print_adj_list() const {
    cout << "\n--- Adjacency List ---" << endl;
    if (n <= 0) {
        cout << "(graph is empty)" << endl;
        return;
    }
    for (int i = 1; i <= n; ++i) {      // 编号是 1..n，0 号位空着不用
        cout << i << " -> ";
        for (int v : adj_list[i]) cout << v << " ";
        cout << endl;
    }
}

void Graph::print_capacity_matrix() const {
    cout << "\n--- Capacity Matrix (0 = no edge) ---" << endl;
    if (n <= 0) {
        cout << "(graph is empty)" << endl;
        return;
    }

    cout << "    ";
    for (int j = 1; j <= n; ++j) cout << j << " ";
    cout << endl;

    for (int i = 1; i <= n; ++i) {
        cout << i << " | ";
        for (int j = 1; j <= n; ++j) {
            cout << cap_matrix[i][j] << " ";
        }
        cout << endl;
    }
}
