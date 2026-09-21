// ============================================================
//  tools/selftest.mjs —— 前端自检（npm run test:core）
//
//  期望值不是拍脑袋写的，是把真正的 C++ 核心（graph.cpp / flow.cpp）
//  跑一遍之后抄下来的，见 tools/parity/ 里的一致性自检。
//  这里不再依赖编译器，纯 JS 就能跑，用来快速确认：
//    1) 兜底实现和 C++ 结果一致（遍历序列、最大流、最小割）
//    2) 序列合法性判别正确
//    3) 文件保存 / 再读回来数据不丢
//    4) 最大流动画里“按增广路累加流量”的算法能算回最终每条边的流量
// ============================================================

import { createFallbackCore } from '../src/wasm/fallback.js'
import { sampleGraphs } from '../src/samples/sampleGraphs.js'

let passed = 0
const failures = []

function ok(name, condition, detail) {
  if (condition) {
    passed += 1
  } else {
    failures.push(`${name}${detail ? ` —— ${detail}` : ''}`)
  }
}

function eq(name, actual, expected) {
  const a = JSON.stringify(actual)
  const b = JSON.stringify(expected)
  ok(name, a === b, `期望 ${b}，实际 ${a}`)
}

const core = createFallbackCore()
const [graph1, graph2, rev] = sampleGraphs

// ------------------------------------------------------------
//  1. graph.txt：6 个点、8 条边、有向带容量
// ------------------------------------------------------------
ok('载入 graph.txt 成功', core.loadText(graph1.text, 'graph.txt').ok)

const snap1 = core.snapshot()
eq('graph.txt 顶点数 / 边数 / 有无向', [snap1.n, snap1.edges.length, snap1.directed], [6, 8, 1])
eq('graph.txt 邻接表第 3 行', snap1.adjList[3], [5])
eq('graph.txt 容量矩阵 1->2', snap1.matrix[1][2], 3)

eq('DFS(1) 序列', core.traverse(0, 1).seq, [1, 2, 3, 5, 6, 4])
eq('BFS(1) 序列', core.traverse(1, 1).seq, [1, 2, 3, 4, 5, 6])
eq('DFS(1) 树边', core.traverse(0, 1).parentEdges, [
  [1, 2],
  [2, 3],
  [2, 4],
  [3, 5],
  [5, 6],
])
eq('从 3 出发只走得到一个连通块', core.traverse(0, 3).reachable, [3, 5, 6])

const flow16 = core.maxFlow(1, 6)
eq('最大流 1->6', [flow16.value, flow16.cutCapacity, flow16.correct], [5, 5, true])
eq('1->6 的增广次数', flow16.steps.length, 5)
eq('1->6 的割边', flow16.cutEdges, [
  [1, 2],
  [1, 3],
])
eq('1->6 的 S / T', [flow16.sourceSide, flow16.sinkSide], [[1], [2, 3, 4, 5, 6]])
eq(
  '1->6 第一次增广',
  flow16.steps[0],
  { path: [1, 2, 3, 5, 6], bottleneck: 1, flowAfter: 1 },
)

// 序列合法性判别
eq(
  '序列 1 2 3 4 5 6：是合法 BFS、不是合法 DFS',
  [core.checkSequence(1, '1 2 3 4 5 6').dfs, core.checkSequence(1, '1 2 3 4 5 6').bfs],
  [false, true],
)
eq(
  '序列 1 2 4 6 3 5：是合法 DFS、不是合法 BFS',
  [core.checkSequence(1, '1 2 4 6 3 5').dfs, core.checkSequence(1, '1 2 4 6 3 5').bfs],
  [true, false],
)
eq(
  '序列 1 3 2 5 6 4：两种都不是',
  [core.checkSequence(1, '1 3 2 5 6 4').dfs, core.checkSequence(1, '1 3 2 5 6 4').bfs],
  [false, false],
)

// ------------------------------------------------------------
//  2. graph2.txt：无向图
// ------------------------------------------------------------
ok('载入 graph2.txt 成功', core.loadText(graph2.text, 'graph2.txt').ok)
const snap2 = core.snapshot()
eq('graph2 是无向图、8 条有向存储合成 6 条边', [snap2.directed, snap2.edges.length], [0, 6])
eq('无向图矩阵是对称的：1-2 和 2-1', [snap2.matrix[1][2], snap2.matrix[2][1]], [1, 1])
eq('无向图 DFS(1)', core.traverse(0, 1).seq, [1, 2, 3, 4, 5])
eq('无向图 BFS(1)', core.traverse(1, 1).seq, [1, 2, 3, 5, 4])
eq('无向图最大流 1->5', core.maxFlow(1, 5).value, 2)

// ------------------------------------------------------------
//  3. rev.txt：专门看反向边“撤回”的那个例子
// ------------------------------------------------------------
ok('载入 rev.txt 成功', core.loadText(rev.text, 'rev.txt').ok)
const flowRev = core.maxFlow(1, 4)
eq('rev.txt 最大流 1->4', [flowRev.value, flowRev.cutCapacity], [2, 2])
eq('rev.txt 第二次增广走了反向边 3->2', flowRev.steps[1].path, [1, 3, 2, 4])
ok(
  '反向边确实是“图上没有的边”（2->3 存在，3->2 原本不存在）',
  flowRev.steps[1].path.includes(3) && flowRev.steps[1].path.includes(2),
)

// ------------------------------------------------------------
//  4. 动画用的“按增广路累加流量”能不能算回最终每条边的流量
//     （FlowAnimation.vue 显示 流量/容量 就是靠这个）
// ------------------------------------------------------------
for (const [name, text, s, t] of [
  ['graph.txt 1->6', graph1.text, 1, 6],
  ['graph2.txt 1->5', graph2.text, 1, 5],
  ['rev.txt 1->4', rev.text, 1, 4],
]) {
  core.loadText(text, name)
  const res = core.maxFlow(s, t)

  const accumulated = new Map()
  for (const step of res.steps) {
    for (let i = 0; i + 1 < step.path.length; i += 1) {
      const a = step.path[i]
      const b = step.path[i + 1]
      accumulated.set(`${a}>${b}`, (accumulated.get(`${a}>${b}`) || 0) + step.bottleneck)
      accumulated.set(`${b}>${a}`, (accumulated.get(`${b}>${a}`) || 0) - step.bottleneck)
    }
  }

  let matched = true
  for (const e of res.edgeFlow) {
    const fromAnimation = Math.max(0, accumulated.get(`${e.u}>${e.v}`) || 0)
    const fromCore = Math.max(0, e.flow)
    if (fromAnimation !== fromCore) matched = false
  }
  ok(`动画累计流量 == 核心算出的流量（${name}）`, matched)
}

// ------------------------------------------------------------
//  5. 保存 / 重新加载：数据不能丢
// ------------------------------------------------------------
core.loadText(graph1.text, 'graph.txt')
const saved = core.saveText()
ok('保存出来的文本第一行是 6 8 1', saved.text.split('\n')[0].trim() === '6 8 1')
core.loadText(saved.text, 'again.txt')
eq('保存再读回来，边表一模一样', core.snapshot().edges, snap1.edges)

// ------------------------------------------------------------
//  6. 错误输入要有明确的提示，不能崩
// ------------------------------------------------------------
ok('空文件会被拒绝', core.loadText('', 'x').ok === false)
ok('第一行格式不对会被拒绝', core.loadText('hello', 'x').ok === false)
ok(
  '边数写多了会被拒绝',
  core.loadText('6 8 1\n1 2\n', 'x').ok === false,
)
ok('端点越界会被拒绝', core.loadText('3 1 1\n1 9\n', 'x').ok === false)
ok('起点越界会被拒绝', core.traverse(0, 99).ok === false)
ok('源点汇点相同会被拒绝', core.maxFlow(1, 1).ok === false)
ok('空序列判别会被拒绝', core.checkSequence(1, '').ok === false)

// ------------------------------------------------------------
//  7. 别人给的图文件要能打开
//     0 开始编号（很多教材、以及早期版本的 graph.txt 都这么写）、
//     注释行、逗号分隔、记事本存的 BOM、第一行没写 0/1 …… 都不能卡住
// ------------------------------------------------------------
{
  // 0 开始编号：4 个点（0..3）、5 条有向边
  const res0 = core.loadText('# 注释行\n4 5 1\n0 1\n1 0\n2 3\n3 0\n0 2\n', 'graph0.txt')
  ok('0 开始编号的文件能打开', res0.ok, res0.error)
  ok('0 开始编号会告诉前端 base = 0', res0.base === 0)

  const snap0 = core.snapshot()
  eq('0 开始编号被映射成 1..n', [snap0.n, snap0.edges.length], [4, 5])
  eq('映射后的邻接表', snap0.adjList, [[], [2, 3], [1], [4], [1]])
  eq('0 开始编号的 DFS(1)', core.traverse(0, 1).seq, [1, 2, 3, 4])
  eq('0 开始编号的最大流 1->4', core.maxFlow(1, 4).value, 1)
  eq(
    '0 开始编号保存出来就是 1 开始编号',
    core.saveText().text,
    '4 5 1\n1 2 1\n1 3 1\n2 1 1\n3 4 1\n4 1 1\n',
  )

  // 0 开始编号 + n 写成了"最大编号"，点数其实是 n + 1
  core.loadText('4 3 1\n0 1\n1 2\n2 4\n', 'x')
  eq('n 写成最大编号时点数算 n + 1', core.snapshot().n, 5)

  // 逗号分隔 / Tab / 多空格
  ok('逗号分隔的文件能打开', core.loadText('4,5,1\n0,1\n1,0\n2,3\n3,0\n0,2\n', 'x').ok)

  // 记事本"另存为 UTF-8"会加 BOM
  ok('带 BOM 的文件能打开', core.loadText('\uFEFF6 8 1\n1 2 3\n1 3 2\n2 3 1\n2 4 2\n3 5 4\n4 3 1\n4 6 3\n5 6 5\n', 'x').ok)

  // 第一行没写第三个数：默认有向图
  const noFlag = core.loadText('4 1\n1 2\n', 'x')
  eq('第一行没写 0/1 时默认是有向图', [noFlag.ok, noFlag.directed], [true, 1])

  // 0 开始编号的文件也不能乱写超范围的编号
  ok('0 开始编号里出现超出 n 的编号会被拒绝', core.loadText('3 2 1\n0 1\n1 5\n', 'x').ok === false)
}

// ------------------------------------------------------------
//  汇总
// ------------------------------------------------------------
console.log(`\n通过 ${passed} 项`)
if (failures.length) {
  console.log(`失败 ${failures.length} 项：`)
  for (const f of failures) console.log(` - ${f}`)
  process.exitCode = 1
} else {
  console.log('前端兜底实现自检全部通过 ✓')
}
