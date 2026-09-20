
#pragma once

#include <string>
#include <vector>

// ============================================================
//  Graph：图的存储与基本操作
//
//  存储方式：
//    adj_list[u]       邻接表，遍历用（u 的所有邻接点）
//    cap_matrix[u][v]  容量矩阵，u->v 的容量，0 表示没有这条边
//
//  编号约定：节点编号一律是 1..n，下标 0 空着不用。
//  邻接表、容量矩阵、随机生成、EasyX 画图全部用同一套编号，
//  否则会出现"图里少一个点"这类很难查的 bug。
// ============================================================
class Graph {
public:
    int n;                                      // 节点个数
    int m;                                      // 边数
    int is_directed;                            // 1 = 有向图，0 = 无向图

    std::vector<std::vector<int>> adj_list;     // adj_list[u] = u 的所有邻接点
    std::vector<std::vector<int>> cap_matrix;   // cap_matrix[u][v] = 边 u->v 的容量

    // directed = true 表示有向图
    explicit Graph(bool directed = true);

    // ---------- 建图 ----------
    // 文件格式：第一行 n m is_directed，之后每行 u v [容量]
    // 容量可以省略，省略就是 1（普通不带权的图）。返回 false 表示读取失败。
    bool build_from_file(const std::string& filename);

    // 随机生成一张图：node_count 个节点、edge_count 条边
    // 节点编号 1..node_count；seed = 0 表示每次都不一样，
    // 填一个非 0 的种子（比如 12345）就能复现出完全相同的一张图。
    // max_capacity > 1 时每条的边的容量在 1..max_capacity 之间随机取（给最大流用），
    // 等于 1 就是不带权的图。
    void build_random(int node_count, int edge_count, bool directed = true, unsigned seed = 0,
                      int max_capacity = 1);

    // 加一条边 u -> v，容量默认 1；无向图会自动补上 v -> u
    void add_edge(int u, int v, int capacity = 1);

    // 把当前图按同样的格式写回文件，方便把随机生成的图存下来复现
    bool save_to_file(const std::string& filename) const;

    // ---------- 遍历 ----------
    // 从 start_node 出发的深度优先遍历，返回访问序列。
    // parent 不为空时，顺便把"每个点是跟着谁走过来的"填进去（画动画要用来高亮走过的边）。
    std::vector<int> get_dfs_sequence(int start_node, std::vector<int>* parent = nullptr) const;

    // 从 start_node 出发的广度优先遍历，返回访问序列（parent 的含义同上）
    std::vector<int> get_bfs_sequence(int start_node, std::vector<int>* parent = nullptr) const;

    // 从 start_node 出发能走到的所有节点（连通块），结果已排序
    std::vector<int> reachable_from(int start_node) const;

    // ---------- 遍历序列判别 ----------
    // 判断用户给的序列是不是一个合法的 DFS / BFS 遍历序列，结果打印到控制台。
    //
    // 注意：DFS / BFS 的合法序列不唯一（邻居的访问顺序换一下，答案就变了），
    // 所以这里不是拿输入和"标准答案"逐个比对，而是按照遍历规则一步一步检查
    // 这个序列真的能不能走出来。这一点答辩时经常被问。
    void check_sequence(const std::vector<int>& user_seq, int start_node) const;

    bool is_valid_dfs(const std::vector<int>& seq, int start_node) const;
    bool is_valid_bfs(const std::vector<int>& seq, int start_node) const;

    // ---------- 输出 ----------
    void print_adj_list() const;
    void print_capacity_matrix() const;

private:
    // 递归的 DFS 辅助函数，只在类内部使用
    void dfs_util(int u, std::vector<bool>& visited, std::vector<int>& seq,
                  std::vector<int>* parent) const;

    // u 是否还有未访问的邻居（判别 DFS 序列用）
    bool has_unvisited_neighbor(int u, const std::vector<bool>& visited) const;
};
