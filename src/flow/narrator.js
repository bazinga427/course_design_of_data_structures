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
//      → 最小割分三步播：① 整理残余网络，擦掉余额 = 0 的弧
//          ② 从 s 只走「余额 > 0」的弧一层层往外扩散，走到的点归 S
//          ③ 剩下的点归 T，把 S→T 的弧标成割边并求和
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
  PRUNE: 'prune',
  GROW: 'grow',
  CUT: 'cut',
}

export const PHASE_LABEL = {
  [PHASE.INIT]: '初始化',
  [PHASE.ROUTE]: '增广路',
  [PHASE.PUSH]: '推流',
  [PHASE.STOP]: '结束判定',
  [PHASE.PRUNE]: '整理残余网络',
  [PHASE.GROW]: '分点集',
  [PHASE.CUT]: '最小割',
}

// 反向弧只在图不太大的时候才画出来，这个上限和 GraphCanvas.vue 里
// buildElements() 的判断保持一致（两边不一致会出现「有键没图元」的空高亮）。
const REVERSE_ARC_LIMIT = 400

// 从 s 扩散最多播这么多帧：层数少就一层一帧，层数多就把相邻几层并成一帧。
const GROW_FRAME_LIMIT = 10

// 把若干层合并成不超过 limit 帧，保证帧数不随图的规模爆炸
function chunkLevels(levels, limit) {
  if (levels.length <= limit) return levels.map((level) => [level])
  const out = []
  const per = Math.ceil(levels.length / limit)
  for (let i = 0; i < levels.length; i += per) out.push(levels.slice(i, i + per))
  return out
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
      droppedEdges: null,
      growInfo: null,
      hideReverseArcs: false,
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

  // 在最终残余网络里从 s 扩散，看 S 侧到底由哪些点组成。
  // 顺便按 BFS 的层次记下「第几层新走到哪些点、用的是哪条弧」，
  // 最小割那几步动画（整理 → 扩散 → 割）直接用这份记录。
  const inS = new Array(n + 1).fill(false)
  const levelOf = new Array(n + 1).fill(-1)
  const growLevels = [[s]]   // growLevels[k]：第 k 层新走到的点（第 0 层只有 s）
  const growArcs = [[]]      // growArcs[k][i]：走到 growLevels[k][i] 用的那条弧
  const queue = [s]
  inS[s] = true
  levelOf[s] = 0
  for (let head = 0; head < queue.length; head += 1) {
    const u = queue[head]
    for (let v = 1; v <= n; v += 1) {
      if (residualAt(finalFlows, u, v) > 0 && !inS[v]) {
        inS[v] = true
        levelOf[v] = levelOf[u] + 1
        queue.push(v)
        if (!growLevels[levelOf[v]]) {
          growLevels[levelOf[v]] = []
          growArcs[levelOf[v]] = []
        }
        growLevels[levelOf[v]].push(v)
        growArcs[levelOf[v]].push([u, v])
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

  // ---- 最小割第 1 步：整理残余网络 ----
  // 余额 = 0 的弧再也承载不了任何流量：推满的正向弧推不动了，撤回量用完的
  // 反向弧也撤不回来了。把它们从图上擦掉，剩下的弧余额都 > 0。
  // （割边正好都在这批被擦掉的弧里：S→T 的弧在残余网络里余额一定是 0。）
  const droppedKeys = []
  const droppedSeen = new Set()
  const pushDropped = (key) => {
    if (droppedSeen.has(key)) return
    droppedSeen.add(key)
    droppedKeys.push(key)
  }
  {
    const edges = (graph && graph.edges) || []
    const listed = new Set(edges.map((e) => `${e.u}_${e.v}`))
    // 反向弧只在图不太大的时候才画出来，和 GraphCanvas 的判断保持一致
    const drawReverse = edges.length <= REVERSE_ARC_LIMIT
    for (const e of edges) {
      if (residualAt(finalFlows, e.u, e.v) <= 0) pushDropped(keyOf(e.u, e.v))
      if (!drawReverse) continue
      if (listed.has(`${e.v}_${e.u}`)) continue // 反方向的弧原图里本来就有
      const backKey = keyOf(e.v, e.u)
      if (backKey.startsWith('rev:') && residualAt(finalFlows, e.v, e.u) <= 0) pushDropped(backKey)
    }
  }

  add({
    kind: 'prune',
    phase: PHASE.PRUNE,
    round: steps.length + 1,
    roundIndex: steps.length,
    stepIndex: steps.length,
    group: '结束与最小割',
    title: '整理残余网络',
    label: `整理残余网络 · 擦掉 ${droppedKeys.length} 条没余额的弧`,
    text:
      `最大流 ${maxFlow} 已经定了。这一步先把残余网络里余额 = 0 的弧擦掉：` +
      `推满的正向弧（再也推不动）和撤回量用完的反向弧（再也撤不回来），一共 ${droppedKeys.length} 条。` +
      '它们不可能再运任何流量，剩下的弧余额都大于 0，下一步只用这些弧往外走。',
    focusNodes: { [s]: 'source' },
    droppedEdges: droppedKeys,
    flows: finalFlows,
    totals: totalsOf(maxFlow),
  })

  // ---- 最小割第 2 步：从 s 只走「余额 > 0」的弧，一层层往外扩散 ----
  // 走得到的点归 S，走不到的归 T。层数多的时候把相邻几层并成一帧，别播太久。
  const growPlan = []
  {
    const levelParts = []
    for (let k = 1; k < growLevels.length; k += 1) {
      const nodes = growLevels[k]
      if (nodes && nodes.length) levelParts.push({ level: k, nodes, arcs: growArcs[k] || [] })
    }
    const chunks = chunkLevels(levelParts, GROW_FRAME_LIMIT)
    // S 里只有 s 一个点（一条余额 > 0 的弧都走不出去）时也要给一帧，讲清楚为什么
    const parts = chunks.length ? chunks : [[{ level: 1, nodes: [], arcs: [] }]]
    let seen = [s]
    const arcs = []
    for (const chunk of parts) {
      const added = []
      for (const part of chunk) {
        for (let i = 0; i < part.nodes.length; i += 1) {
          const v = part.nodes[i]
          const arc = part.arcs[i] || [s, v]
          arcs.push(arc)
          added.push({ v, from: arc[0] })
        }
      }
      seen = seen.concat(added.map((a) => a.v))
      growPlan.push({
        level: chunk[chunk.length - 1].level,
        added,
        arcs: arcs.slice(),
        seen: seen.slice(),
      })
    }
  }

  for (let i = 0; i < growPlan.length; i += 1) {
    const plan = growPlan[i]
    const focusNodes = {}
    for (const v of plan.seen) focusNodes[v] = 'setS'
    for (const a of plan.added) focusNodes[a.v] = 'current'
    const focusEdges = {}
    for (const arc of plan.arcs) focusEdges[keyOf(arc[0], arc[1])] = 'tree'
    const addedNodes = plan.added.map((a) => a.v)
    const what = addedNodes.length
      ? `这一层新走到 { ${addedNodes.join(', ')} }`
      : `从 ${s} 出发，余额 > 0 的弧一条都走不出去`

    add({
      kind: 'grow',
      phase: PHASE.GROW,
      round: steps.length + 1,
      roundIndex: steps.length,
      stepIndex: steps.length,
      group: '结束与最小割',
      title: `分点集 · 第 ${plan.level} 层`,
      label: `S 扩散 · 累计 ${plan.seen.length} 个点`,
      text:
        `从 ${s} 出发，只沿余额 > 0 的弧往外走（${what}，走的就是图上青色那些弧）。` +
        `到这一层为止，确定归 S 的点有 { ${plan.seen.join(', ')} } —— ` +
        '它们都是残余网络里真的还能从 s 走到的点，走不到的等到最后一步一起归 T。',
      focusNodes,
      focusEdges,
      changes: plan.added.map((a) => ({ u: a.from, v: a.v, dir: 'reach' })),
      growInfo: { level: plan.level, added: addedNodes, seen: plan.seen.slice() },
      flows: finalFlows,
      totals: totalsOf(maxFlow),
    })
  }

  // ---- 最小割第 3 步：剩下的点归 T，S→T 的弧就是割边 ----
  // 怎么分、割边是谁、容量和多少
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
    // 到这一步反向弧的使命（撤流）已经结束：一键收起来，画面只剩 S / T / 割边
    hideReverseArcs: true,
    cutSummary: cutParts.length ? `${cutParts.join(' + ')} = ${cutSum}` : `无割边，${cutSum}`,
  })

  return { frames, verified, reason: verified ? '' : 'cut-mismatch' }
}
