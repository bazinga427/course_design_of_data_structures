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
//    5) 过程解说（src/flow/narrator.js）重放出来的增广路、残余网络、
//       最小割，和核心给的每一步完全一致
// ============================================================

import { createFallbackCore } from '../src/wasm/fallback.js'
import { sampleGraphs } from '../src/samples/sampleGraphs.js'
import { buildNarration, buildResidualView, arcKey } from '../src/flow/narrator.js'
import { store, edgeKey, reverseEdgeKey } from '../src/store/graphStore.js'
import {
  SUPPLY_CHAIN_SAMPLE,
  analyzeLineImpact,
  buildGraphTextWithExtraCapacity,
  buildSupplyChainSummary,
  formatSupplyRoute,
} from '../src/domain/supplyChain.js'

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
//  8. 过程解说：重放出来的增广路 / 残余网络 / 最小割必须和核心一致
//     （右侧「过程解说」栏就是拿这些帧渲染的）
// ------------------------------------------------------------
for (const [name, text, s, t] of [
  ['graph.txt 1->6', graph1.text, 1, 6],
  ['graph2.txt 1->5', graph2.text, 1, 5],
  ['rev.txt 1->4', rev.text, 1, 4],
]) {
  core.loadText(text, name)
  const snap = core.snapshot()
  const res = core.maxFlow(s, t)
  const { frames, verified } = buildNarration(res, snap)

  ok(`解说重放和核心一致（${name}）`, verified === true)
  ok(`解说帧不为空（${name}）`, frames.length > 0)

  eq(
    `解说的增广路和核心一样（${name}）`,
    frames.filter((f) => f.kind === 'route').map((f) => f.path),
    res.steps.map((st) => st.path),
  )
  eq(
    `解说的瓶颈和核心一样（${name}）`,
    frames.filter((f) => f.kind === 'route').map((f) => f.bottleneck),
    res.steps.map((st) => st.bottleneck),
  )
  ok(
    `沿路每条弧的余额都正好减掉瓶颈（${name}）`,
    frames
      .filter((f) => f.kind === 'push')
      .every((f) => f.balance && f.balance.before - f.balance.after === f.bottleneck),
  )

  const last = frames[frames.length - 1]
  eq(
    `解说最后给出的 S / T 和核心一样（${name}）`,
    [last.sets.S, last.sets.T],
    [res.sourceSide, res.sinkSide],
  )
  eq(`解说最后给出的割边和核心一样（${name}）`, last.cutEdges, res.cutEdges)

  // 最小割不能只是“最后闪一下”，必须一步一步来：整理 → 扩散 → 割
  const capOf = (u, v) => {
    if (snap.matrix) return (snap.matrix[u] && snap.matrix[u][v]) || 0
    const e = snap.edges.find((x) => x.u === u && x.v === v)
    return e ? e.cap : 0
  }
  const pruneFrames = frames.filter((f) => f.kind === 'prune')
  const growFrames = frames.filter((f) => f.kind === 'grow')
  eq(`最小割第一步只有一帧“整理残余网络”（${name}）`, pruneFrames.length, 1)
  ok(`最小割有“分点集”的扩散帧（${name}）`, growFrames.length >= 1)
  ok(`最后一帧才是割边那一帧（${name}）`, last.kind === 'cut')
  ok(`割边那一帧会把反向弧收起来（${name}）`, last.hideReverseArcs === true)
  ok(
    `擦掉的弧余额确实都是 0（${name}）`,
    pruneFrames.every((f) =>
      f.droppedEdges.every((key) => {
        const [u, v] = key.replace(/^rev:/, '').split('_').map(Number)
        return capOf(u, v) - (last.flows[arcKey(u, v)] || 0) <= 0
      }),
    ),
  )
  ok(
    `扩散出来的点都落在核心给的 S 里（${name}）`,
    growFrames.every((f) => f.growInfo.seen.every((v) => res.sourceSide.includes(v))),
  )
  eq(
    `扩散到最后一层，S 正好等于核心给的 S 侧（${name}）`,
    [...growFrames[growFrames.length - 1].growInfo.seen].sort((a, b) => a - b),
    res.sourceSide,
  )

  // 帧里重放出的净流量 == 核心算出的每条弧的流量
  let flowMatched = true
  for (const e of res.edgeFlow) {
    const net = last.flows[arcKey(e.u, e.v)] || 0
    if (net !== e.flow) flowMatched = false
  }
  ok(`解说重放的净流量 == 核心的流量（${name}）`, flowMatched)

  // 中间每一帧的累计流量都要 ≤ 最大流，而且不能倒退
  let monotone = true
  let previous = 0
  for (const f of frames) {
    const flow = f.totals ? f.totals.flow : 0
    if (flow < previous) monotone = false
    if (flow > res.value) monotone = false
    previous = flow
  }
  ok(`解说的总流量单调不减且不超过最大流（${name}）`, monotone)

  // 每一帧的字段都要齐全，右侧解说栏直接拿这些渲染
  const fieldsOk = frames.every(
    (f) =>
      typeof f.kind === 'string' &&
      typeof f.phase === 'string' &&
      typeof f.group === 'string' &&
      typeof f.title === 'string' &&
      typeof f.label === 'string' &&
      typeof f.text === 'string' &&
      f.focusNodes &&
      f.focusEdges &&
      Array.isArray(f.changes) &&
      f.flows &&
      f.totals,
  )
  ok(`每一帧的字段都齐全（${name}）`, fieldsOk)
}

// 反向边撤流的细节：rev.txt 第二轮沿 3→2 推流（图上本来没有这条弧），
// 所以 3→2 的残余容量应该减少，而 2→3 的残余容量应该“加”回来
{
  core.loadText(rev.text, 'rev.txt')
  const snap = core.snapshot()
  const res = core.maxFlow(1, 4)
  const { frames } = buildNarration(res, snap)
  const target = frames.find(
    (f) => f.kind === 'push' && f.balance && f.balance.u === 3 && f.balance.v === 2,
  )

  ok('解说里有沿反向弧 3→2 推流的那一步', !!target)
  if (target) {
    const routeFrame = frames.find((f) => f.kind === 'route' && f.round === target.round)
    const netBefore = routeFrame ? routeFrame.flows['2,3'] || 0 : 0
    const netAfter = target.flows['2,3'] || 0
    ok('3→2 的余额被扣减', target.balance.after < target.balance.before)
    ok('扣减量 == 瓶颈', target.balance.delta === target.bottleneck)
    ok('反向弧 2→3 的净流量被撤回', netBefore - netAfter === target.bottleneck)
    ok('推反向弧时用的是反向弧专用的图层键', 'rev:3_2' in target.focusEdges)
  }
}

// ------------------------------------------------------------
//  8b. 残量网络视图：点「残量网络」按钮画到画布上的那张图，
//      必须就是「核心的最终残量网络 + 最小割」
// ------------------------------------------------------------
const capAt = (snap, u, v) => {
  if (snap.matrix && snap.matrix[u]) return snap.matrix[u][v] || 0
  const e = snap.edges.find((x) => x.u === u && x.v === v)
  return e ? e.cap : 0
}

// ------------------------------------------------------------
//  8c. 最小割动画：三步的顺序、擦掉的弧、S 是层序扩散出来的
// ------------------------------------------------------------
{
  core.loadText(graph1.text, 'graph.txt')
  const snapCut = core.snapshot()
  const resCut = core.maxFlow(1, 6)
  const { frames } = buildNarration(resCut, snapCut)
  const prune = frames.find((f) => f.kind === 'prune')
  eq(
    '最小割三步的帧序是 整理 → 扩散 → 割',
    frames.slice(frames.indexOf(prune)).map((f) => f.kind),
    ['prune', 'grow', 'cut'],
  )
  eq(
    '整理这一步擦掉的弧里包含两条割边 1_2 / 1_3',
    prune.droppedEdges.filter((k) => k === '1_2' || k === '1_3').sort(),
    ['1_2', '1_3'],
  )
  ok(
    '擦掉的只是一部分弧，没把整张图擦光',
    prune.droppedEdges.length > 0 && prune.droppedEdges.length < snapCut.edges.length,
  )
  ok(
    '整理残余网络这一帧带着“擦掉哪几条弧”的清单',
    Array.isArray(prune.droppedEdges) && prune.droppedEdges.length > 0,
  )

  // rev.txt：第二轮沿 3→2 推流之后，反向弧 3→2 的余额变成 0（甚至为负），
  // 它就是“没用到的反向弧”，整理那一步要把它擦掉
  core.loadText(rev.text, 'rev.txt')
  const resRev = core.maxFlow(1, 4)
  const { frames: revFrames } = buildNarration(resRev, core.snapshot())
  const revPrune = revFrames.find((f) => f.kind === 'prune')
  ok('rev.txt 里余额用完的反向弧 rev:3_2 会被擦掉', revPrune.droppedEdges.includes('rev:3_2'))
}

// ------------------------------------------------------------
//  8d. 残量网络视图与解说图层键必须都能在画布上找到
// ------------------------------------------------------------
for (const [name, text, s, t] of [
  ['graph.txt 1->6', graph1.text, 1, 6],
  ['graph2.txt 1->5', graph2.text, 1, 5],
  ['rev.txt 1->4', rev.text, 1, 4],
]) {
  core.loadText(text, name)
  const snap = core.snapshot()
  const res = core.maxFlow(s, t)
  const view = buildResidualView(res, snap)

  ok(`残量网络视图能生成（${name}）`, !!view)
  if (view) {
    ok(`重放出的 S 侧 == 核心的 S 侧（${name}）`, view.verified === true)
    eq(`残量网络的 S / T 和核心一样（${name}）`, [view.sets.S, view.sets.T], [res.sourceSide, res.sinkSide])
    eq(`残量网络的割边和核心一样（${name}）`, view.cutEdges, res.cutEdges)
    eq(
      `残量网络的最大流 / 最小割和核心一样（${name}）`,
      [view.maxFlow, view.cutCapacity],
      [res.value, res.cutCapacity],
    )

    // 每条正向弧按残余量分类：余量 > 0 → residualArc「余 r」；余量 0 → zeroResidual
    const cutKeys = new Set(view.cutEdges.map(([u, v]) => `${u}_${v}`))
    let arcsOk = true
    let cutOk = true
    for (const e of snap.edges) {
      const k = `${e.u}_${e.v}`
      if (cutKeys.has(k)) {
        if (view.edges[k] !== 'cut') cutOk = false
        continue
      }
      const r = capAt(snap, e.u, e.v) - (view.flow[arcKey(e.u, e.v)] || 0)
      if (r > 0) {
        if (view.edges[k] !== 'residualArc' || view.edgeLabels[k] !== `余 ${r}`) arcsOk = false
      } else if (view.edges[k] !== 'zeroResidual') {
        arcsOk = false
      }
    }
    ok(`每条正向弧都按余量分类正确（${name}）`, arcsOk)
    ok(`每条割边都标成了 cut（${name}）`, cutOk)

    // 割边一定已经饱和（容量 - 净流量 == 0）—— 这就是「最大流 = 最小割」的来源
    ok(
      `割边都已经饱和（${name}）`,
      view.cutEdges.every(
        ([u, v]) => capAt(snap, u, v) - (view.flow[arcKey(u, v)] || 0) === 0,
      ),
    )

    // 反向弧：原图里没有这条弧、且净流量 > 0 时才画，余额 = 能撤回的流量
    let revOk = true
    for (const e of snap.edges) {
      if (snap.edges.some((x) => x.u === e.v && x.v === e.u)) continue
      const label = view.edgeLabels[`rev:${e.v}_${e.u}`]
      const r = capAt(snap, e.v, e.u) - (view.flow[arcKey(e.v, e.u)] || 0)
      if (r > 0) {
        if (label !== `余 ${r}`) revOk = false
      } else if (label !== undefined) {
        revOk = false
      }
    }
    ok(`反向弧的余额 == 可撤回的流量（${name}）`, revOk)
  }

  store.graph = snap // edgeKey / reverseEdgeKey 要看有向无向
  const { frames } = buildNarration(res, snap)

  // 照 GraphCanvas.buildElements() 的规则，列出画布上真实存在的图层键
  const listed = new Set(snap.edges.map((e) => `${e.u}_${e.v}`))
  const valid = new Set()
  for (const e of snap.edges) {
    valid.add(edgeKey(e.u, e.v))
    if (snap.edges.length <= 400 && !listed.has(`${e.v}_${e.u}`)) {
      valid.add(reverseEdgeKey(e.v, e.u))
    }
  }

  const keys = []
  for (const f of frames) {
    keys.push(...Object.keys(f.focusEdges || {}))
    keys.push(...(f.droppedEdges || []))
  }
  eq(
    `解说帧用到的图层键在画布上都有图元（${name}）`,
    keys.filter((k) => !valid.has(k)),
    [],
  )
}

// ------------------------------------------------------------
//  9. 供应链应用：最大可交付量、瓶颈割集、扩容模拟
// ------------------------------------------------------------
core.loadText(SUPPLY_CHAIN_SAMPLE.text, 'supply-chain')
const scSnap = core.snapshot()
eq('供应链示例规模', [scSnap.n, scSnap.edges.length, scSnap.directed], [12, 19, 1])

const scRes = core.maxFlow(SUPPLY_CHAIN_SAMPLE.source, SUPPLY_CHAIN_SAMPLE.sink)
eq('供应链示例最大流 = 最小割', [scRes.value, scRes.cutCapacity, scRes.correct], [19, 19, true])

const scSummary = buildSupplyChainSummary(scRes, scSnap, SUPPLY_CHAIN_SAMPLE)
ok('供应链摘要可生成', !!scSummary)
eq(
  '供应链瓶颈线路',
  scSummary.cutEdges.map((e) => e.key),
  ['4_6', '5_8', '7_9', '7_10'],
)
eq(
  '供应链瓶颈容量之和 = 最大可交付量',
  scSummary.cutEdges.reduce((sum, e) => sum + e.cap, 0),
  19,
)
ok('瓶颈线路会翻译成业务名称', scSummary.cutEdges[0].label.includes('上海总仓'))
eq(
  '补货路径会翻译成节点简称',
  formatSupplyRoute([1, 2, 4, 6, 9], SUPPLY_CHAIN_SAMPLE),
  '供应 → 华东厂 → 上海仓 → 苏州站 → 上海店',
)

const expanded = buildGraphTextWithExtraCapacity(scSnap, 4, 6, 5)
core.loadText(expanded, 'supply-chain-expanded')
const scExpanded = core.maxFlow(SUPPLY_CHAIN_SAMPLE.source, SUPPLY_CHAIN_SAMPLE.sink)
ok('扩容瓶颈线路后最大可交付量提升', scExpanded.value > scRes.value)

// ------------------------------------------------------------
//  10. 问题定位：线路影响分析（改一条线 → 重算 → 和基线对比）
// ------------------------------------------------------------
const analysisCore = createFallbackCore()
analysisCore.loadText(SUPPLY_CHAIN_SAMPLE.text, 'supply-chain')
const analysisGraph = analysisCore.snapshot()
const analysisBase = {
  originalText: SUPPLY_CHAIN_SAMPLE.text,
  source: SUPPLY_CHAIN_SAMPLE.source,
  sink: SUPPLY_CHAIN_SAMPLE.sink,
}

const failAnalysis = analyzeLineImpact(analysisCore, analysisGraph, SUPPLY_CHAIN_SAMPLE, {
  ...analysisBase,
  mode: 'fail',
})
ok('线路分析：中断模式能算出结果', failAnalysis.ok)
eq('线路分析：基线最大流 = 19', failAnalysis.base, 19)
eq('线路分析：缺口 = 需求 24 - 基线 19', failAnalysis.gap, 5)
ok(
  '线路分析：最脆弱的是 1→2（影响 11）',
  !!failAnalysis.summary.worst &&
    failAnalysis.summary.worst.key === '1_2' &&
    failAnalysis.summary.worst.impact === 11 &&
    failAnalysis.summary.worst.isCut === false,
)
ok(
  '线路分析：8→10 是冗余线路（断了也不影响）',
  failAnalysis.rows.some((r) => r.key === '8_10' && r.impact === 0),
)
ok(
  '线路分析：瓶颈线路被标出来（4 条割边都在结果里）',
  ['4_6', '5_8', '7_9', '7_10'].every((k) =>
    failAnalysis.rows.some((r) => r.key === k && r.isCut),
  ),
)

const halfAnalysis = analyzeLineImpact(analysisCore, analysisGraph, SUPPLY_CHAIN_SAMPLE, {
  ...analysisBase,
  mode: 'degrade',
  ratio: 0.5,
})
ok(
  '线路分析：运力降到 50% 时 1→2 仍然最敏感',
  !!halfAnalysis.summary.worst && halfAnalysis.summary.worst.key === '1_2',
)
eq('线路分析：降级比例会出现在结果里', halfAnalysis.ratio, 0.5)

// 情景参数可以改：降到 80% 的影响应该小于降到 50%
const mildAnalysis = analyzeLineImpact(analysisCore, analysisGraph, SUPPLY_CHAIN_SAMPLE, {
  ...analysisBase,
  mode: 'degrade',
  ratio: 0.8,
})
ok(
  '线路分析：降级到 80% 的影响小于降到 50%',
  mildAnalysis.summary.worst.impact < halfAnalysis.summary.worst.impact,
)

const expandAnalysis = analyzeLineImpact(analysisCore, analysisGraph, SUPPLY_CHAIN_SAMPLE, {
  ...analysisBase,
  mode: 'expand',
  delta: 5,
})
eq('线路分析：扩容 +5 只有 4 条线路有效', expandAnalysis.summary.affected.length, 4)
eq(
  '线路分析：扩容 +5 每条提升 1',
  expandAnalysis.summary.affected.every((r) => r.impact === 1),
  true,
)
eq(
  '线路分析：有效的正好是 4 条瓶颈割边',
  expandAnalysis.summary.affected
    .map((r) => [r.u, r.v])
    .sort((a, b) => a[0] - b[0] || a[1] - b[1]),
  [
    [4, 6],
    [5, 8],
    [7, 9],
    [7, 10],
  ],
)

// 扩容增量也能改：+10 的提升不会小于 +5（扩容不会让最大流变小）
const bigExpand = analyzeLineImpact(analysisCore, analysisGraph, SUPPLY_CHAIN_SAMPLE, {
  ...analysisBase,
  mode: 'expand',
  delta: 10,
})
ok(
  '线路分析：扩容 +10 的提升不小于 +5',
  bigExpand.summary.worst.impact >= expandAnalysis.summary.worst.impact,
)

// 关键回归：分析是在“影子图”上做的，跑完必须把核心还原成原始图
const restoredSnap = analysisCore.snapshot()
eq(
  '线路分析：跑完后核心图已还原（边数不变）',
  restoredSnap.edges.length,
  analysisGraph.edges.length,
)
eq('线路分析：还原后最大流仍是 19', analysisCore.maxFlow(1, 12).value, 19)

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
