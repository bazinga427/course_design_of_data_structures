# 图结构课程设计 —— 网页版

把同一套图算法（建图 / 存储 / DFS / BFS / 遍历序列判别 / 最大流最小割 /
随机建图 / 保存加载）搬到浏览器里：左边是数据面板和算法动画，右边是图结构可视化。
页面里内置了一个“物流与供应链”应用场景，把工厂、仓库、配送站、门店解释成
最大流网络，用最小割指出瓶颈线路，并支持对瓶颈线路做扩容模拟。

算法内核是 **C++17**（`cpp/graph.cpp` / `cpp/flow.cpp` /
`cpp/graph_io.cpp`），网页版给它套了一层 `binding.cpp`（JSON 接口）并编译成 WASM；
没有装 Emscripten 的时候自动退回 `src/wasm/fallback.js` 里的 JS 等价实现，页面照样能跑。

## 代码都在哪

| 位置 | 是什么 |
| --- | --- |
| `index.html` / `vite.config.js` / `package.json` | 前端工程配置（Vite） |
| `src/main.js`、`src/App.vue` | 页面入口与整体布局 |
| `src/components/GraphCanvas.vue` | **图结构可视化**：Cytoscape 画布、5 种布局、点大小、缩放、边权标签 / 残量网络 / 反向边三个显示开关、导出 PNG |
| `src/components/StoragePanel.vue` | 邻接表 / 邻接矩阵（容量矩阵）/ 边表 |
| `src/components/TraversalPanel.vue` | DFS / BFS 计算、单步播放、遍历序列合法性判别 |
| `src/components/FlowAnimation.vue` | 最大流最小割计算、按增广路播放、残余网络视图、一键看最小割（整理 → 分点集 → 割边） |
| `src/components/SupplyChainPanel.vue` | **物流与供应链应用**：载入示例、最大可交付量、瓶颈割集、扩容模拟、线路利用率 |
| `src/domain/supplyChain.js` | 供应链业务层：节点角色、线路名称、业务解释和供应链示例数据 |
| `src/store/graphStore.js` | 全页面共用的状态（当前图、两个高亮图层、提示条） |
| `src/wasm/loader.js` | 拿算法接口：优先 WASM，没有就退回 JS |
| `src/wasm/fallback.js` | C++ 核心的 JS 等价实现（读文件逻辑和 `graph_io.cpp` 一一对应） |
| `src/samples/sampleGraphs.js` | 页面上「载入示例」的三张图 |
| `binding.cpp` | **后端适配层**：C++ 核心 ↔ 前端，返回 JSON 文本 |
| `cpp/` | 算法源码：`graph.{h,cpp}`、`flow.{h,cpp}`、`graph_io.{h,cpp}`、`layout.h` |
| `tools/build_wasm.bat` | 把 C++ 核心编译成 `public/wasm/graph_core.js` |
| `tools/selftest.mjs` | 纯 JS 自检（`npm run test:core`） |
| `tools/parity/` | C++ 与 JS 两边结果逐字段对比的一致性自检 |
| `graph.txt`、`graph2.txt`、`rev.txt`、`graph0.txt` | 示例 / 自检用的图文件（`graph0.txt` 是 0 开始编号的例子） |
| `FRONTEND.md` | 上面 8 个接口的 JSON 字段说明 |

## 怎么跑

```bat
npm install
npm run dev        :: 开发模式，http://localhost:5173
npm run build      :: 打包到 dist\
npm run preview    :: 预览打包结果，http://localhost:4173
```

打包出来的 `dist/` 用的是相对路径，丢到任何静态服务器（甚至 IIS、Nginx、
PyCharm 内置服务器）里都能跑。

## 技术栈

| 层 | 用了什么 |
| --- | --- |
| 前端框架 | Vue 3（`<script setup>` 组合式 API）+ Vite 5 |
| 图可视化 | Cytoscape.js（内置布局 cose 力导向 / circle / grid / breadthfirst / random） |
| 算法 | C++17：邻接表 + 容量矩阵、DFS / BFS、序列判别、Ford-Fulkerson 最大流最小割、随机建图（mt19937） |
| 前后端桥 | Emscripten 把 C++ 编成 WASM，`ccall` + JSON 文本；没有 WASM 时用 JS 兜底实现 |
| 工程化 | 相对路径打包、两条自检（纯 JS 自检 + C++/JS 一致性对比） |

## 物流与供应链应用

左侧“物流与供应链”面板可以直接载入供应链示例。示例把节点分成：

- 供应能力 / 工厂：源点一侧，容量表示可供应或可出厂的数量
- 总仓 / 区域仓 / 配送站：中间节点，表示仓储与分拨能力
- 门店 / 交付需求：汇点一侧，表示最终交付需求

点击“载入供应链示例”后，页面会自动计算最大流和最小割，并显示：

- 最大可交付量：当前供应链网络每周期最多能交付多少
- 瓶颈容量：最小割容量，和最大可交付量相等
- 瓶颈线路：哪些线路共同限制了整体运输能力
- 扩容模拟：选一条瓶颈线路增加容量，重新计算最大流提升多少
- 关键线路利用率：当前流量 / 线路容量，快速看出哪些线路已经满载

## 图文件格式

第一行 `n m is_directed`（顶点数 边数 1=有向 / 0=无向），后面每行一条边 `u v [容量]`，
容量省略就是 1：

```
6 8 1
1 2 3
1 3 2
...
```

读文件的时候比“教科书版”宽容一些，为了能直接打开别人给的图文件：

- **编号从 0 开始的文件也认**（自动 +1 映射成 1..n 再画，页面会提示一句），
  很多教材和早期版本的 `graph.txt` 都是 0 开始编号；
- 第一行前面允许有空行、注释行、UTF-8 BOM（记事本“另存为 UTF-8”会加）；
- 空格 / Tab / 逗号 / 分号 都能当分隔符，`#`、`%`、`//` 后面的内容当注释；
- 容量、第三个数（是否有向）都可以省。

## 几个实现上的说明

**点的大小**：Cytoscape 里节点的 `width` / `height` 是“模型坐标”，会跟着画布
缩放一起放大缩小，而每种布局的包围盒不一样、`fit` 之后的缩放也不一样，
于是同一个点在力导向布局里 72px、在随机布局里能到 136px。现在所有尺寸都乘上
`1/当前缩放`（`GraphCanvas.vue` 里的 `buildStylesheet`），按屏幕像素来算，
点在任何布局、任何缩放下都是同一个大小（默认 28px，工具栏「点大小」可调）。

**最小割的动画**：最大流算完之后不会直接跳出一张割边图，而是按三步播（`src/flow/narrator.js`
里 `prune` / `grow` / `cut` 三种帧）：

1. **整理残余网络**：把余额 = 0 的弧擦掉 —— 推满的正向弧推不动了、撤回量用完的反向弧
   也撤不回来了，这些弧在图上变成灰色虚线；
2. **分点集**：从 s 出发，只沿「余额 > 0」的弧（青色）一层层往外走（BFS 层序），
   走到的点标成 S（蓝色），刚走到的点用橙色点出来；
3. **割**：剩下没走到的点归 T（紫色），原图里所有 S→T 的弧标成粉色割边，
   容量和正好等于最大流。

割边一定都在第 1 步被擦掉的那批弧里 —— 因为 S→T 的弧在残余网络里的余额必然是 0，
这正是「最大流 = 最小割」在程序里的样子。面板上的「一键看最小割」直接跳到这三步重播。

**力导向自动降级**：点数超过 260 时 cose 布局（O(n²)）太慢，自动换成环形布局并提示。

**邻接矩阵上限**：`n > 150` 时矩阵不铺表格（`matrixOmitted`），算法照常计算。

## 自检

```bat
npm run test:core             :: 纯 JS：122 项（遍历序列、最大流、最小割、最小割动画帧、供应链示例、保存再读回、各种图文件）
node tools/parity/compare.mjs :: 编译 C++ 核心跑一遍，和 JS 兜底实现逐字段对比（160 项）
```
