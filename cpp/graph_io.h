#pragma once

#include "graph.h"
#include "layout.h"

#include <string>
#include <utility>
#include <vector>

// ============================================================
//  graph_io.h
//  「图结构的保存与加载」以及几个给界面用的小工具。
//
//  文件格式和队友写的 build_from_file 完全一样，没有另外发明格式：
//      第一行：n m is_directed        （节点数 边数 是否是有向图）
//      之后 m 行：u v                 （每条边两个端点）
//  这样别人读得懂，老师也读得懂，用记事本就能改。
//
//  读取的时候比 build_from_file 宽容一些，为了能直接打开别人给的图文件：
//      * 第一行前面可以有空行、注释行、UTF-8 BOM（记事本另存为 UTF-8 就会加）
//      * 编号从 0 开始的文件也认（自动 +1 映射成 1..n），见 is_zero_based
//      * 空格 / Tab / 逗号 / 分号都能当分隔符，# % // 后面的内容当注释
//  这份逻辑在网页版的 src/wasm/fallback.js 里有一份等价的 JS 实现，改一个要改两个。
//
//  另外节点坐标会单独存一份 <图文件>.layout，避免每次打开都重排位置。
//  这个附属文件没有也不影响使用（缺了就自动重新布局）。
// ============================================================

namespace graph_io {

// 图里出现过的所有节点编号（升序）。
// 仓库里的 graph.txt 用的是 0 开始编号，graph2.txt 用的是 1 开始编号，
// 这两种都能正确认出来。
std::vector<int> node_ids(const Graph& g);

// 判断这张图是不是从 0 开始编号的（0 号点出现在某条边上就是）
bool is_zero_based(const Graph& g);

// 去掉重复之后的边表。
// 无向图的邻接表里同一条边存了两遍（u->v 和 v->u），这里只保留 u<=v 的一份。
std::vector<std::pair<int, int>> edge_list(const Graph& g);

// 读图：先把文件整个读进来检查格式是否合法（顺便把 0 开始编号统一换算成
// 1 开始编号），通过了再按 1..n 建表 —— 不会因为文件里编号越界而把程序写崩，
// 也不会出现"点编号 0 被当成数组下标 -1"这种内存问题。
// 成功返回 true；失败返回 false，失败原因写在 error 里。
//
// base_out 不为空时，会顺带告诉调用方这份文件的编号基准：
//   1 = 文件里的编号就是 1..n；0 = 文件用的是 0 开始编号（已经帮你 +1 了）。
bool load_graph(Graph& g, const std::string& path, std::string& error, int* base_out = nullptr);

// 存图：按上面的格式重新写一份（边会去重、m 会按实际边数写）。
bool save_graph(const Graph& g, const std::string& path, std::string& error);

// 节点坐标文件的路径：<图文件>.layout
std::string layout_path_of(const std::string& graph_path);

// 读写节点坐标。load_layout 要求图里每个节点都能找到坐标，否则返回 false（调用方重新布局）。
bool load_layout(const std::string& graph_path, std::vector<NodePos>& pos);
bool save_layout(const std::string& graph_path, const std::vector<NodePos>& pos);

}  // namespace graph_io
