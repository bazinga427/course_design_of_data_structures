#pragma once
#include <vector>
#include <string>

class Graph {
public:
    int n;                                      // 节点个数
    int m;                                      // 边数
    int is_directed;                            // 1 = 有向图，0 = 无向图

    std::vector<std::vector<int>> adj_list;     // 邻接表 adj_list[u] = u 的所有邻接点
    std::vector<std::vector<int>> adj_matrix;   // 邻接矩阵 adj_matrix[u][v] = 1 表示有边

    // 构造函数：directed = true 表示有向图
    Graph(bool directed = true);

    // ---------- 建图 ----------
    // 从文件读入图（文件格式：第一行 n m is_directed，之后每行 u v）
    void build_from_file(std::string filename);

    // 加一条边 u -> v（无向图会自动补上 v -> u）
    void add_edge(int u, int v);

    // ---------- 遍历 ----------
    // 从 start_node 出发的深度优先遍历，返回访问序列
    std::vector<int> get_dfs_sequence(int start_node);

    // 从 start_node 出发的广度优先遍历，返回访问序列
    std::vector<int> get_bfs_sequence(int start_node);

    // ---------- 判别 ----------
    // 判断用户给的序列是 DFS 还是 BFS，结果直接打印到控制台
    void check_sequence_simple(const std::vector<int>& user_seq, int start_node);

    // ---------- 输出 ----------
    // 把邻接表打印到控制台
    void print_graph();

private:
    // 递归的 DFS 辅助函数，只在类内部使用
    void dfs_util(int u, std::vector<bool>& visited, std::vector<int>& seq);
};
