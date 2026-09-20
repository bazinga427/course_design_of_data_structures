#pragma once

#include <string>

// 打开「图结构可视化」窗口（用 EasyX 画）。
//
// 窗口里可以做这些事：
//   * 画图：节点画成圆圈、边画成直线（有向图带箭头），能看出邻接关系
//   * 拖拽：鼠标左键按住节点就能把节点拖到别的位置
//   * 缩放：鼠标滚轮以光标为中心放大 / 缩小，也可以点工具栏的放大缩小
//   * 滚动条：右边和下边各有一条，拖滑块就能看图的其它部分
//   * 文件：工具栏「打开图文件 / 保存」读写图结构文件（格式和其他模块完全一致）
//
// initial_file 为空时，自动尝试打开当前目录下的 graph.txt / graph2.txt。
void run_graph_ui(const std::string& initial_file = std::string());
