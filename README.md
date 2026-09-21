# 图结构课程设计（桌面版 + 网页版）

这个文件夹里是同一个课程设计的两份实现，**算法源码是同一份**：

| 目录 | 是什么 | 怎么跑 |
| --- | --- | --- |
| `course_design_of_data_structures-main\` | C++ / EasyX 桌面版（控制台菜单 + 图形窗口） | 双击 `build.bat`，或按 `main.exe 1` 直接开图形窗口 |
| `graph-web\` | 网页版（Vue 3 + Vite + Cytoscape.js，算法核心是 C++ 编译出的 WASM，没装 Emscripten 时用 JS 等价实现） | `npm install` 然后 `npm run dev`；看打包结果用 `npm run build` + `npm run preview`（http://localhost:4173） |

两份实现共用下面这些 C++ 文件（网页版把它们放在 `graph-web\cpp\`，
通过 `graph-web\binding.cpp` 暴露成 JSON 接口，编译成 WASM）：

```
graph.h / graph.cpp        图的存储（邻接表 + 容量矩阵）、DFS / BFS、遍历序列判别、随机建图
flow.h  / flow.cpp         Ford-Fulkerson 最大流 + 最小割（带每一次增广的过程）
graph_io.h / graph_io.cpp  图文件的保存与加载、节点编号识别（1 开始 / 0 开始）
layout.h / layout.cpp      桌面版的圆形 / 随机 / 力导向布局
main.cpp / ui.cpp          桌面版的控制台菜单与 EasyX 窗口
```

网页版自己的说明（目录结构、JSON 接口契约、怎么自检）在
`graph-web\README.md` 和 `graph-web\FRONTEND.md` 里。

读图文件的规则在两边是同一套：第一行 `n m is_directed`，后面每行 `u v [容量]`；
**0 开始编号的文件也会自动 +1 映射成 1..n**，另外空行、`#`/`%`/`//` 注释行、
逗号或分号分隔、UTF-8 BOM 都能容忍。
