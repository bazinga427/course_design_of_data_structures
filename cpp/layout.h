#pragma once

#include "graph.h"

#include <vector>

// 节点在「世界坐标」下的位置。
// 世界坐标是一套和窗口无关的平面坐标（图自己的坐标系），
// 由 ui.cpp 里的相机负责换算成屏幕像素，所以缩放、平移都不会改变这里的数据。
struct NodePos {
    int id;         // 节点编号，和 Graph 里的编号一致
    double x, y;    // 位置
};

// 圆形布局：把节点均匀摆在圆周上，点数少的时候最整齐，也是默认布局。
void layout_circle(std::vector<NodePos>& pos);

// 随机布局：给一份随机的初始摆放，用来对比「布局不一样，画出来也不一样」。
void layout_random(std::vector<NodePos>& pos, unsigned int seed);

// 力导向布局（Fruchterman-Reingold 的简化版）：
//   边像弹簧，把相连的点拉近；任意两点之间又有斥力，把点推开。
// 迭代若干轮之后，连接紧密的点会聚在一起，图的结构一眼就能看出来。
// pos 里的编号必须在 g 里存在；点数多的时候请少传一点 iterations，否则界面会卡。
void layout_force(std::vector<NodePos>& pos, const Graph& g, int iterations = 400);

// 把布局结果整体平移、缩放到「以原点为中心、最远点距离约等于 radius」的范围内。
// 每种布局算出来的坐标尺度都不一样，先归一化再交给相机，画出来的大小才稳定。
void layout_normalize(std::vector<NodePos>& pos, double radius);
