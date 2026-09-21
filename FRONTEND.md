# 网页版与 C++ 核心之间的接口契约

网页版自己不写算法：算法全部在 C++ 里（`cpp/graph.cpp`、`cpp/flow.cpp`、
`cpp/graph_io.cpp`），通过 `binding.cpp` 暴露成下面 8 个函数，
再用 Emscripten 编译成 `public/wasm/graph_core.js`（WASM）。

- 前端调用入口：`src/wasm/loader.js`（优先 WASM，没有就退回 `src/wasm/fallback.js`）
- C++ 适配层：`binding.cpp`（只做“参数搬运 + JSON 拼装”，不写算法）
- JS 等价实现：`src/wasm/fallback.js`（接口、字段、算法顺序都和 C++ 一致）

约定：

1. 所有函数都返回**一段 JSON 文本**，字段名两边完全一样；
2. 图上所有编号一律是 **1..n**（0 开始编号的文件由 `graph_io::load_graph`
   在读文件时自动 +1 映射掉）；
3. 出错时统一返回 `{ "ok": false, "error": "……" }`，前端直接弹出这段话。

## gv_version()

```json
{ "ok": true, "name": "graph_core", "version": "1.0.0", "maxNodes": 2000, "matrixLimit": 150 }
```

## gv_load_text(text, fileName)

用文件内容建图（“打开图文件”）。

```json
{ "ok": true, "fileName": "graph.txt", "n": 6, "m": 8, "directed": 1, "base": 1 }
```

`base` = 1 表示文件里的编号就是 1..n；= 0 表示文件用的是 0 开始编号，
核心已经自动 +1 映射过（前端会提示一句“已自动映射成 1..n”）。

读文件时兼容：BOM、空行、`#` / `%` / `//` 注释行、逗号或分号分隔、
容量省略（按 1 处理）、第三个数（是否有向）省略（按有向处理）。

## gv_random(n, m, directed, seed, maxCapacity)

随机生成一张图（“创建随机图文件”）。`seed = 0` 表示每次都不同，
否则同一个 `seed` 每次生成的图完全一样（写报告、答辩复现同一张图用）。

```json
{ "ok": true, "n": 8, "m": 14, "directed": 1, "seed": 12345, "maxCapacity": 9 }
```

## gv_save_text()

把当前图导出成和 `graph.txt` 一样的文本（“保存图文件”）。

```json
{ "ok": true, "text": "6 8 1\n1 2 3\n……" }
```

## gv_snapshot()

当前这张图的全部信息，页面右侧的存储结构面板、画布都靠它。

```json
{
  "ok": true,
  "n": 6,
  "m": 8,
  "directed": 1,
  "nodes": [1, 2, 3, 4, 5, 6],
  "edges": [{ "u": 1, "v": 2, "cap": 3 }],
  "adjList": [[], [2, 3], [3, 4]],
  "matrixOmitted": false,
  "matrix": [[0, 0], [0, 0]]
}
```

- `adjList[0]` 空着不用，`adjList[u]` 就是 u 的所有邻居，顺序和算法里一致；
- `matrix[u][v]` 就是 u→v 的容量（0 表示没有这条边）；
- `n > 150` 时矩阵太大，`matrixOmitted` 为 true、`matrix` 为 null（算法照算，只是不铺表格）。

## gv_traverse(mode, start)

`mode = 0` 是 DFS，`mode = 1` 是 BFS。遍历动画就靠 `seq` + `parentEdges`。

```json
{
  "ok": true,
  "mode": 0,
  "start": 1,
  "seq": [1, 2, 3, 5, 6, 4],
  "reachable": [1, 2, 3, 4, 5, 6],
  "parentEdges": [[1, 2], [2, 3], [2, 4], [3, 5], [5, 6]]
}
```

## gv_check_sequence(start, seqText)

判断用户输入的序列是不是合法的 DFS / BFS 序列。判的不是“和标准答案一样”，
而是“按遍历规则能不能真的走出来”（合法答案本来就不止一个）。

```json
{
  "ok": true,
  "start": 1,
  "seq": [1, 2, 3, 4, 5, 6],
  "dfs": false,
  "bfs": true,
  "sampleDfs": [1, 2, 3, 5, 6, 4],
  "sampleBfs": [1, 2, 3, 4, 5, 6]
}
```

## gv_max_flow(s, t)

Ford-Fulkerson 最大流 + 最小割，`steps` 里是每一次增广，动画按它播。

```json
{
  "ok": true,
  "s": 1,
  "t": 6,
  "value": 5,
  "cutCapacity": 5,
  "correct": true,
  "steps": [{ "path": [1, 2, 3, 5, 6], "bottleneck": 1, "flowAfter": 1 }],
  "sourceSide": [1],
  "sinkSide": [2, 3, 4, 5, 6],
  "cutEdges": [[1, 2], [1, 3]],
  "edgeFlow": [{ "u": 1, "v": 2, "cap": 3, "flow": 2 }]
}
```

`correct` 是程序自己校的：最小割容量是不是等于最大流（最大流最小割定理）。
`edgeFlow` 里的 `flow` 是净流量，无向图可能出现负数，前端显示时按 0 处理。

## 这两个核心怎么保证算得一样？

```bat
node tools/parity/compare.mjs
```

这条命令会用 g++ 把真正的 C++ 核心编译出来跑一遍，再让 JS 兜底实现跑一遍，
把每个接口的返回值逐字段对比（当前 160 项，全部一致）。改动任何一边之后
都建议跑一次；只想跑纯 JS 的自检用 `npm run test:core`。
