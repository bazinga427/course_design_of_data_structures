// ============================================================
//  src/flow/narrator.js —— 最大流过程的逐帧解说生成器
//
//  这个文件不碰算法核心，只读 gv_max_flow() 的返回值（steps / sourceSide /
//  cutEdges …），再把残余网络重放一遍，产出「一条增广路一条增广路、
//  一条弧一条弧」的解说帧：
//
//    初始化
//      → 第 1 条增广路：路线 + 瓶颈
//          → 沿这条路每条弧一帧：当前 a→b，余额从多少减到多少
//      → 第 2 条增广路：……
//      → 残余网络里 s 走不到 t 了，结束
//      → 最小割：S / T 怎么分、割边是哪几条、容量和是多少
//
//  为什么前端能重放得和核心一模一样：
//    核心每次增广只做两件事：正向弧的残余容量减 bottleneck、反向弧加
//    bottleneck。这个更新顺序在 cpp/flow.cpp 和 src/wasm/fallback.js 里
//    完全一致，所以照 steps 重放出来的残余网络就是核心内部的残余网络，
//    每条弧的“余额”都能对着讲。
//
//  兜底：重放出来的 S 侧必须等于核心给的 sourceSide、割边容量之和必须
//  等于核心给的 cutCapacity。对不上就不讲最小割，只播路线和推流，并把
//  verified 标成 false，界面上会提示。
//
//  这个文件是纯函数、不依赖 Vue，tools/selftest.mjs 里能直接跑。
// ============================================================

export const PHASE = {
  INIT: 'init',
  ROUTE: 'route',
  PUSH: 'push',
  STOP: 'stop',
  CUT: 'cut',
}

export const PHASE_LABEL = {
  [PHASE.INIT]: '初始化',
  [PHASE.ROUTE]: '增广路',
  [PHASE.PUSH]: '推流',
  [PHASE.STOP]: '结束判定',
  [PHASE.CUT]: '最小割',
}

export function arcKey(u, v) {
  return `${u},${v}`
}

// 画布按 edgeKey 取高亮样式：无向图里 u-v 和 v-u 是同一条边，统一成小号在前。
// 默认实现和 src/store/graphStore.js 的 edgeKey 保持一致，调用方也可以传自己的。
export function makeEdgeKeyer(graph) {
  const directed = graph ? graph.directed : 1
  const listed = new Set(((graph && graph.edges) || []).map((e) => `${e.u}_${e.v}`))
  return (u, v) => {
    if (!directed && v < u) return `${v}_${u}`
    const forward = `${u}_${v}`
    if (listed.has(forward)) return forward
    // 这条弧原图里没有：它只能以“反向弧”的身份出现在残余网络里，
    // 所以用和正向弧分开的键，免得无向图把两个方向合并成同一条。
    return listed.has(`${v}_${u}`) ? `rev:${u}_${v}` : forward
  }
}

// 容量查询：优先用 snapshot.matrix（下标 1..n 的 (n+1)×(n+1) 容量矩阵），
// 矩阵因为太大被省略时（n > 150）用边表重建；无向图两个方向都有容量。
export function makeCapLookup(graph) {
  if (!graph) return () => 0
  const matrix = graph.matrix
  if (matrix && matrix.length) {
    return (u, v) => {
      const row = matrix[u]
      return row && row[v] ? row[v] : 0
    }
  }
  const map = new Map()
  for (const e of graph.edges || []) {
    map.set(arcKey(e.u, e.v), e.cap)
    if (!graph.directed) map.set(arcKey(e.v, e.u), e.cap)
  }
  return (u, v) => map.get(arcKey(u, v)) || 0
}

// 残余网络 = 容量 − 净流量。只记“被推过流”的弧，没记的净流量就是 0，
// 这样 n 很大时也不会铺一张 n×n 的表。
//
// push(u, v, b)：沿 u→v 推 b 个单位的流
//   → 正方向净流量 +b（余额因此减 b），反方向净流量 −b（余额加 b）
function makeResidual(cap) {
  const net = new Map()
  return {
    residual(u, v) {
      return cap(u, v) - (net.get(arcKey(u, v)) || 0)
    },
    push(u, v, amount) {
      net.set(arcKey(u, v), (net.get(arcKey(u, v)) || 0) + amount)
      net.set(arcKey(v, u), (net.get(arcKey(v, u)) || 0) - amount)
    },
    snapshot() {
      return Object.fromEntries(net)
    },
  }
}

function sameSet(a, b) {
  if (!a || !b || a.length !== b.length) return false
  const sa = [...a].sort((x, y) => x - y)
  const sb = [...b].sort((x, y) => x - y)
  return sa.every((v, i) => v === sb[i])
}

/**
 * 把一次最大流的结果翻译成一串解说帧。
 *
 * @param {object} result 核心 gv_max_flow() 的返回值
 * @param {object} graph  核心 gv_snapshot() 的返回值
 * @param {object} [options] { keyOf }
 * @returns {{frames: object[], verified: boolean, reason: string}}
 */
export function buildNarration(result, graph, options = {}) {
  if (!result || !result.ok || !graph) {
    return { frames: [], verified: true, reason: 'no-result' }
  }

  const keyOf = options.keyOf || makeEdgeKeyer(graph)
  const cap = makeCapLookup(graph)
  const residual = makeResidual(cap)
  const n = graph.n
  const s = result.s
  const t = result.t
  const steps = Array.isArray(result.steps) ? result.steps : []
  const maxFlow = result.value
  const cutCapacity = result.cutCapacity

  const frames = []
  const add = (frame) => {
    frames.push({
      id: frames.length,
      round: 0,
      roundIndex: -1,
      stepIndex: 0,
      group: '准备',
      path: [],
      focusNodes: {},
      focusEdges: {},
      focusArc: null,
      focusResidual: null,
      balance: null,
      changes: [],
      flows: {},
      sets: null,
      cutEdges: null,
      cutSummary: '',
      bottleneck: null,
      ...frame,
    })
  }
  const totalsOf = (flow) => ({ flow, maxFlow, rounds: steps.length, cutCapacity })
  const residualAt = (flows, u, v) => cap(u, v) - (flows[arcKey(u, v)] || 0)
  const pathEdges = (path, cls) => {
    const out = {}
    for (let k = 0; k + 1 < path.length; k += 1) out[keyOf(path[k], path[k + 1])] = cls
    return out
  }

  // ---- 初始化 ----
  add({
    kind: 'init',
    phase: PHASE.INIT,
    title: '初始化',
    label: '初始化 · 余额 = 容量',
    text: `初始化：残余网络 = 容量矩阵，所有弧的余额 = 容量，总流量 0。下面从 s = ${s} 开始推流到 t = ${t}。`,
    flows: residual.snapshot(),
    totals: totalsOf(0),
  })

  // ---- 先把每条增广路重放一遍，同时验证和核心一致 ----
  let verified = true
  let flow = 0
  const plans = []
  for (let i = 0; i < steps.length; i += 1) {
    const step = steps[i]
    const before = residual.snapshot()
    const flowBefore = flow
    for (let k = 0; k + 1 < step.path.length; k += 1) {
      residual.push(step.path[k], step.path[k + 1], step.bottleneck)
    }
    flow += step.bottleneck
    plans.push({ step, before, flowBefore })
  }
  const finalFlows = residual.snapshot()

  // 在最终残余网络里从 s 扩散，看 S 侧到底由哪些点组成
  const inS = new Array(n + 1).fill(false)
  const queue = [s]
  inS[s] = true
  for (let head = 0; head < queue.length; head += 1) {
    const u = queue[head]
    for (let v = 1; v <= n; v += 1) {
      if (residualAt(finalFlows, u, v) > 0 && !inS[v]) {
        inS[v] = true
        queue.push(v)
      }
    }
  }
  const sourceSide = []
  const sinkSide = []
  for (let v = 1; v <= n; v += 1) (inS[v] ? sourceSide : sinkSide).push(v)
  if (!sameSet(sourceSide, result.sourceSide || [])) verified = false

  const cutEdges = Array.isArray(result.cutEdges) ? result.cutEdges : []
  let cutSum = 0
  const cutParts = []
  for (const [u, v] of cutEdges) {
    const c = cap(u, v)
    cutSum += c
    cutParts.push(`${u}→${v}(${c})`)
  }
  if (cutSum !== cutCapacity) verified = false

  // ---- 每条增广路：路线一帧 + 沿路每条弧一帧 ----
  for (let i = 0; i < plans.length; i += 1) {
    const { step, before, flowBefore } = plans[i]
    const round = i + 1
    const group = `第 ${round} 条增广路`
    const pathText = step.path.join('→')

    add({
      kind: 'route',
      phase: PHASE.ROUTE,
      round,
      roundIndex: i,
      stepIndex: i,
      group,
      title: `第 ${round} 条增广路`,
      label: `路线 ${pathText}`,
      text: `第 ${round} 条增广路：${pathText}，瓶颈 ${step.bottleneck}。`,
      path: step.path,
      bottleneck: step.bottleneck,
      focusEdges: pathEdges(step.path, 'path'),
      flows: before,
      totals: totalsOf(flowBefore),
    })

    // 沿这条路一条弧一条弧地推：每条弧的余额都减 bottleneck
    let current = { ...before }
    for (let k = 0; k + 1 < step.path.length; k += 1) {
      const a = step.path[k]
      const b = step.path[k + 1]
      const beforeR = residualAt(current, a, b)
      current = {
        ...current,
        [arcKey(a, b)]: (current[arcKey(a, b)] || 0) + step.bottleneck,
        [arcKey(b, a)]: (current[arcKey(b, a)] || 0) - step.bottleneck,
      }
      const afterR = residualAt(current, a, b)
      const isLast = k + 2 === step.path.length

      add({
        kind: 'push',
        phase: PHASE.PUSH,
        round,
        roundIndex: i,
        stepIndex: i,
        group,
        title: `当前 ${a} → ${b}`,
        label: `${a}→${b} 余额 ${beforeR}→${afterR}`,
        text: `当前 ${a} → ${b}：余额 ${beforeR} 减 ${step.bottleneck}，变成 ${afterR}。`,
        path: step.path,
        bottleneck: step.bottleneck,
        focusArc: [a, b],
        focusResidual: afterR,
        balance: { u: a, v: b, before: beforeR, after: afterR, delta: beforeR - afterR },
        changes: [{ u: a, v: b, dir: 'fwd', before: beforeR, after: afterR }],
        focusEdges: { ...pathEdges(step.path, 'path'), [keyOf(a, b)]: 'update' },
        flows: current,
        totals: totalsOf(isLast ? step.flowAfter : flowBefore),
      })
    }
  }

  // ---- 结束判定 ----
  add({
    kind: 'noPath',
    phase: PHASE.STOP,
    round: steps.length + 1,
    roundIndex: steps.length,
    stepIndex: steps.length,
    group: '结束与最小割',
    title: '结束判定',
    label: `结束判定 · ${s} 走不到 ${t}`,
    text: `残余网络里从 ${s} 已经走不到 ${t} 了，没有增广路，最大流 = ${maxFlow}。`,
    focusNodes: { [s]: 'source' },
    flows: finalFlows,
    totals: totalsOf(maxFlow),
  })

  // ---- 最小割：怎么分、割边是谁、容量和多少 ----
  const cutNodes = {}
  for (const v of sourceSide) cutNodes[v] = 'setS'
  for (const v of sinkSide) cutNodes[v] = 'setT'
  const cutMarks = {}
  for (const [u, v] of cutEdges) cutMarks[keyOf(u, v)] = 'cut'

  add({
    kind: 'cut',
    phase: PHASE.CUT,
    round: steps.length + 1,
    roundIndex: steps.length,
    stepIndex: steps.length,
    group: '结束与最小割',
    title: `最小割 = ${cutCapacity}`,
    label: `割 ${cutCapacity}：S={${sourceSide.join(',')}}`,
    text: `在最终残余网络里从 ${s} 还能走到的点组成 S = { ${sourceSide.join(
      ', ',
    )} }，剩下的组成 T = { ${sinkSide.join(', ')} }。割边是从 S 指向 T 的弧：${
      cutParts.length ? cutParts.join(' + ') : '没有这样的弧'
    }，容量和 = ${cutSum} = 最大流 ${maxFlow}。`,
    focusNodes: cutNodes,
    focusEdges: cutMarks,
    flows: finalFlows,
    totals: totalsOf(maxFlow),
    sets: { S: sourceSide, T: sinkSide },
    cutEdges,
    cutSummary: cutParts.length ? `${cutParts.join(' + ')} = ${cutSum}` : `无割边，${cutSum}`,
  })

  return { frames, verified, reason: verified ? '' : 'cut-mismatch' }
}

/**
 * 残量网络视图：最大流演示跑完之后「定格」的那张残量网络，
 * 整理成画布图层（nodes / edges / edgeLabels）要的数据。
 *
 * 和讲解帧的区别：它只有一张终态快照，不参与时间轴。画布按它渲染就能看到：
 *   * 还有余量的正向弧 —— 标「余 r」
 *   * 已经饱和的正向弧 —— 残量网络里它没了，只留一条淡灰的边
 *   * 反向弧 —— 原图没有、但推过流之后可以撤回的那条，标「余 r」
 *   * 割边 —— S 指向 T 的边（一定已经饱和），标「割 c」
 * 顺便把 S / T 两侧也标出来，这张图就是「残量网络 + 最小割」。
 *
 * @param {object} result 核心 gv_max_flow() 的返回值
 * @param {object} graph  核心 gv_snapshot() 的返回值
 * @param {object} [options] { keyOf }
 * @returns {null | {
 *   nodes: object, edges: object, edgeLabels: object, flow: object,
 *   sets: { S: number[], T: number[] }, cutEdges: number[][],
 *   cutCapacity: number, maxFlow: number, verified: boolean,
 * }}
 */
export function buildResidualView(result, graph, options = {}) {
  if (!result || !result.ok || !graph) return null

  const keyOf = options.keyOf || makeEdgeKeyer(graph)
  const cap = makeCapLookup(graph)
  const residual = makeResidual(cap)
  const n = graph.n
  const s = result.s
  const t = result.t
  const steps = Array.isArray(result.steps) ? result.steps : []
  const maxFlow = result.value
  const cutCapacity = result.cutCapacity

  // 把所有增广路重放一遍，得到最终净流量（和 buildNarration 用的是同一套重放）
  for (const step of steps) {
    for (let k = 0; k + 1 < step.path.length; k += 1) {
      residual.push(step.path[k], step.path[k + 1], step.bottleneck)
    }
  }
  const flow = residual.snapshot()
  const residualOf = (u, v) => cap(u, v) - (flow[arcKey(u, v)] || 0)

  // S 侧 = 在最终残量网络里从 s 还能走到的点；剩下的就是 T 侧
  const inS = new Array(n + 1).fill(false)
  const queue = [s]
  inS[s] = true
  for (let head = 0; head < queue.length; head += 1) {
    const u = queue[head]
    for (let v = 1; v <= n; v += 1) {
      if (residualOf(u, v) > 0 && !inS[v]) {
        inS[v] = true
        queue.push(v)
      }
    }
  }

  const nodes = {}
  const sourceSide = []
  const sinkSide = []
  for (let v = 1; v <= n; v += 1) {
    if (inS[v]) {
      nodes[v] = 'setS'
      sourceSide.push(v)
    } else {
      nodes[v] = 'setT'
      sinkSide.push(v)
    }
  }

  // 割边 = 原图里从 S 指向 T 的边（这些边在残量网络里一定已经饱和）
  const cutEdges = Array.isArray(result.cutEdges) ? result.cutEdges : []
  const cutSet = new Set(cutEdges.map(([u, v]) => keyOf(u, v)))

  const edges = {}
  const edgeLabels = {}
  const listed = new Set(graph.edges.map((e) => `${e.u}_${e.v}`))

  for (const e of graph.edges) {
    const k = keyOf(e.u, e.v)
    if (cutSet.has(k)) {
      // 割边：从 S 指向 T，容量就是它贡献给最小割的那一份
      edges[k] = 'cut'
      edgeLabels[k] = `割 ${cap(e.u, e.v)}`
      continue
    }
    const r = residualOf(e.u, e.v)
    if (r > 0) {
      edges[k] = 'residualArc'
      edgeLabels[k] = `余 ${r}`
    } else {
      // 饱和了：残量网络里这条正向弧已经不存在
      edges[k] = 'zeroResidual'
      edgeLabels[k] = '余 0'
    }
  }

  // 反向弧：正方向推过多少流，残量网络里就有一条余额 = 净流量的反向弧
  // （原图里没有这条弧，画布上画成青蓝虚线，余额表示能撤回来多少）
  if (graph.edges.length <= 400) {
    for (const e of graph.edges) {
      if (listed.has(`${e.v}_${e.u}`)) continue
      const r = residualOf(e.v, e.u)
      if (r > 0) edgeLabels[`rev:${e.v}_${e.u}`] = `余 ${r}`
    }
  }

  return {
    nodes,
    edges,
    edgeLabels,
    flow,
    sets: { S: sourceSide, T: sinkSide },
    cutEdges,
    cutCapacity,
    maxFlow,
    // 和核心给的 S 侧对一下，对不上说明重放有问题，界面可以提示
    verified: sameSet(sourceSide, result.sourceSide || []),
  }
}
