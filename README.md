# 图结构课程设计

用 C++ 实现图的建立、存储、遍历、遍历序列判别、最大流最小割，并带一个 EasyX 图形窗口显示图结构。

## 功能一览

| 功能 | 说明 | 代码位置 |
| --- | --- | --- |
| 建图 | 从文件读入，或随机生成（可指定种子复现同一张图） | `graph.cpp` |
| 存储 | 邻接表（遍历用）+ 容量矩阵（最大流用） | `graph.cpp` |
| 遍历 | DFS、BFS，起点可以自己指定 | `graph.cpp` |
| 判别 | 判断一个序列是不是合法的 DFS / BFS 遍历序列 | `graph.cpp` |
| 最大流 / 最小割 | Ford-Fulkerson，顺便求出最小割并自动校验两者相等 | `flow.cpp` |
| 可视化 | EasyX 窗口：拖拽、缩放、滚动条、自动布局、打开/保存图文件、导出图片 | `ui.cpp` |
| 布局 | 圆环布局、随机布局、力导向布局（Fruchterman-Reingold） | `layout.cpp` |
| 文件读写 | 图的保存与加载、节点坐标文件、节点编号识别 | `graph_io.cpp` |

## 环境要求

- Windows，64 位。EasyX 基于 Win32 GDI，只能在 Windows 上用。
- 64 位 MinGW-w64 的 `g++`（本项目在 g++ 11.4.0 / x86_64-posix-seh 上验证通过）。
- EasyX 已经放在 `third_party/easyx` 里，**不需要另外安装**。

有一个编译器相关的坑：仓库里的 `libeasyx.a` 是用 msvcrt 版编译器编出来的。
如果你用 **UCRT 版** MinGW（`g++ -v` 里能看到 `ucrt` 字样），需要额外的 `tools/easyx_ucrt_compat.c`
来补 `__imp___iob_func` 这个符号。`build.bat` 里已经写了判断：有这个文件就带上，没有就跳过，
所以两种编译器都能编。**不要**用 `-lmsvcrt-os` 去补这个符号，那会让 msvcrt 和 UCRT 混在同一个进程里，
带 `-static` 的程序会直接堆损坏崩溃。

## 编译与运行

三种方式任选：

1. 双击 `build.bat`（编译完会问你要不要直接打开图形窗口）。
2. 用 VSCode 打开本文件夹，按 `Ctrl+Shift+B`。
3. 命令行手动敲：

```
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -I third_party/easyx/include -L third_party/easyx/lib64 main.cpp graph.cpp flow.cpp ui.cpp layout.cpp graph_io.cpp -o main.exe -leasyx -lcomdlg32
```

编译出来的 `main.exe` 是静态链接的，只用 Windows 自带的系统 DLL，拷到任何一台 64 位 Windows 上
都能直接跑，对方不需要装 MinGW，也不需要装 EasyX。程序用相对路径读数据文件，
所以 `main.exe` 要和 `graph.txt`、`graph2.txt` 放在同一个文件夹里。

运行方式：

| 命令 | 效果 |
| --- | --- |
| `main.exe` | 进入控制台菜单 |
| `main.exe 1` | 直接打开 EasyX 图形窗口 |
| 菜单第 10 项 | 从控制台菜单打开图形窗口，窗口里显示的就是菜单里当前这张图 |

## 控制台菜单

```
==============================
 Graph course design
 Current graph: N = 6, M = 8, directed  [graph.txt]
------------------------------
 1. Load graph from a file
 2. Generate a random graph
 3. Show adjacency list
 4. Show capacity matrix
 5. Show DFS order
 6. Show BFS order
 7. Check a DFS / BFS sequence
 8. Save current graph to a file
 9. Max flow / min cut (Ford-Fulkerson)
10. Open the EasyX window (graph view)
 0. Exit
==============================
```

菜单文字用英文是故意的：Windows 控制台默认是 GBK 代码页，直接把 UTF-8 的中文 `cout` 出来是乱码。
图形窗口里的中文没问题，`ui.cpp` 里做了 UTF-8 到 GBK 的转换。

## 数据文件格式

第一行是 `n m is_directed`（节点数、边数、1 = 有向图 / 0 = 无向图），
后面每行一条边 `u v [容量]`，容量可以不写，不写就是 1。**节点编号从 1 开始。**

```
6 8 1
1 2 3
1 3 2
...
```

带容量是为了最大流最小割：容量矩阵同时给遍历和最大流用，不需要两套数据。
仓库里带了两张示例图：`graph.txt`（6 个点、有向、带容量）、`graph2.txt`（5 个点、无向）。

## 最大流最小割（Ford-Fulkerson）

菜单第 9 项，输入源点和汇点。算法只有四步：

1. 残留网络一开始就等于容量矩阵。
2. 在残留网络里找一条从源点到汇点的增广路（路上每条边的剩余容量都大于 0）。
3. 这条路上能推的流量 = 路上最小的剩余容量（瓶颈）。
4. 沿路更新残留网络：正向边剩余容量减掉瓶颈，**反向边加上瓶颈**，回到第 2 步；
   找不到增广路时，累计的流量就是最大流。

第 4 步里的"反向边"是最容易看不懂的地方：它的作用是**撤销**之前的错误决定。
如果某一趟选的路把边占满了、后来发现更好的走法要用到这条边，反向边就让流量退回去重新分配。

最小割是顺便求出来的：在最后的残留网络里，从源点出发能走到的点组成 S，走不到的点组成 T，
从 S 指向 T 的边就是割边。程序会打印割边和割边容量之和，并自动检查它是否等于最大流
（最大流最小割定理）。相等就说明这次计算是对的，答辩时可以直接拿这一行当验证。

每一次增广都记录在 `FlowResult::steps` 里（路径 + 瓶颈 + 累计流量），以后做动画直接按这个播。

## EasyX 图形窗口

运行 `main.exe 1` 或菜单第 10 项打开。窗口里可以：

- **显示**：节点画成圆圈并标编号，边画成直线，有向图带箭头；自环和双向边也能分清楚。
- **拖拽节点**：鼠标左键按住节点拖动。
- **缩放**：滚轮以光标为中心缩放（15% ~ 600%），工具栏也有放大 / 缩小 / 适应窗口。
- **滚动条**：右侧和下方各一条，内容超出窗口时拖滑块浏览（也可以用鼠标拖空白处平移）。
- **选中节点**：单击节点会高亮它和它的邻居、相邻的边，并显示这个节点的邻接表。
- **自动布局**：`R` 力导向布局（相连的点会靠拢）、`C` 圆环布局。
- **文件**：工具栏或 `Ctrl+O` / `Ctrl+S`；节点坐标另存一份 `<文件名>.layout`，下次打开位置不变。
- **导出图片**：`Ctrl+P` 或 `P`，写报告可以直接贴图。
- 窗口里按 `H` 看完整操作说明。

实现要点（写报告可以直接用）：

1. **世界坐标 + 相机**：节点位置存在与窗口无关的"世界坐标"里，
   屏幕像素 =（世界坐标 − 相机左上角）× 缩放比例。缩放只改比例、平移只改相机左上角，
   滚动条也换算成相机左上角，所以三者互不干扰，缩放时鼠标指着的那个点不会跑。
2. **按帧刷新**：每帧先用 `peekmessage` 取走全部消息，再整体重画，最后 `FlushBatchDraw`。
   用 `BeginBatchDraw` 双缓冲，避免闪烁；用 `Sleep(16)` 控制在 60 帧左右，不占满 CPU。
3. **布局和绘制分开**：`layout.cpp` 只算坐标，不碰图形库，可以单独测试。
4. **中文显示**：`ui.cpp` 里的 `gbk()` 把源码里的 UTF-8 转成 EasyX 需要的 GBK 再输出。

## 可复现性

- 随机生成会把用到的种子打印出来，填同一个种子就能得到完全一样的图。
- 菜单第 8 项可以把当前图存成文件（含容量），以后直接按文件读入。
- 依赖库 EasyX 已经提交进仓库，队友 clone 下来不需要额外装环境就能编译。
- `main.exe`、`view_graph.txt`、`saved_graph.txt` 这些是编译或运行产物，已经被 `.gitignore` 忽略；
  交作业时再把 exe 和项目一起拷。

## 还没做的部分

- 图形窗口里目前只有"看图"功能（拖拽 / 缩放 / 布局 / 文件读写），
  遍历动画和最大流最小割的展示还没接到窗口里，只在控制台里出结果。
  数据都是现成的：`get_dfs_sequence` / `get_bfs_sequence` 给动画播放表，`FlowResult::steps` 给增广过程。
- 强连通分量（作业要求里如果要用，可以复用 `Graph::reachable_from`）。

## 文件说明

```
graph.h / graph.cpp      图的存储与算法（邻接表 + 容量矩阵、遍历、序列判别）
flow.h / flow.cpp        最大流最小割（Ford-Fulkerson）
ui.h / ui.cpp            EasyX 可视化窗口
layout.h / layout.cpp    节点布局（圆环 / 随机 / 力导向）
graph_io.h / graph_io.cpp  图文件的读写、节点坐标、编号识别
main.cpp                 控制台菜单 + 启动图形窗口
graph.txt / graph2.txt   示例图
build.bat                一键编译
.vscode/                 编辑器配置（Ctrl+Shift+B 编译）
third_party/easyx/       EasyX 头文件和静态库
tools/                   EasyX 的 UCRT 兼容补丁（如果编译器需要的话）
```
