// ============================================================
//  supplyChain.js —— 物流与供应链业务层
//
//  这里不实现最大流算法，只把图上的点和边翻译成供应链语义：
//    * 节点：供应能力 / 工厂 / 仓库 / 配送站 / 门店 / 交付需求
//    * 边：运输线路，容量表示这条线路在一个周期内最多能运多少
//    * 结果：最大可交付量、瓶颈割集、线路利用率、扩容模拟
//
//  真正计算仍由 C++/WASM/JS 核心完成，前端只做业务解释。
// ============================================================

export const SUPPLY_ROLE_LABELS = {
  supplier: '供应能力',
  factory: '工厂',
  warehouse: '总仓',
  regional: '区域仓',
  dc: '配送站',
  store: '门店',
  demand: '交付需求',
}

export const SUPPLY_CHAIN_SAMPLE = {
  id: 'supply-chain',
  name: '供应链示例（工厂 → 仓库 → 配送站 → 门店）',
  tip: '推荐源点 1，汇点 12；容量表示线路每周期可运输量',
  source: 1,
  sink: 12,
  totalDemand: 24,
  unit: '单位',
  text: `12 19 1
1 2 14
1 3 10
2 4 8
2 5 4
3 4 3
3 5 5
4 6 6
4 7 6
5 7 5
5 8 5
6 9 5
6 10 4
7 9 4
7 10 4
8 10 3
8 11 6
9 12 8
10 12 8
11 12 8
`,
  nodes: {
    1: { type: 'supplier', name: '供应能力', short: '供应' },
    2: { type: 'factory', name: '华东工厂', short: '华东厂' },
    3: { type: 'factory', name: '华南工厂', short: '华南厂' },
    4: { type: 'warehouse', name: '上海总仓', short: '上海仓' },
    5: { type: 'regional', name: '南京区域仓', short: '南京仓' },
    6: { type: 'dc', name: '苏州配送站', short: '苏州站' },
    7: { type: 'dc', name: '杭州配送站', short: '杭州站' },
    8: { type: 'dc', name: '宁波配送站', short: '宁波站' },
    9: { type: 'store', name: '上海门店', short: '上海店' },
    10: { type: 'store', name: '杭州门店', short: '杭州店' },
    11: { type: 'store', name: '宁波门店', short: '宁波店' },
    12: { type: 'demand', name: '交付需求', short: '需求' },
  },
  edges: {
    '1_2': { name: '供应能力 → 华东工厂', mode: '供应', leadTime: 0 },
    '1_3': { name: '供应能力 → 华南工厂', mode: '供应', leadTime: 0 },
    '2_4': { name: '华东工厂 → 上海总仓', mode: '干线', leadTime: 2 },
    '2_5': { name: '华东工厂 → 南京仓', mode: '干线', leadTime: 2 },
    '3_4': { name: '华南工厂 → 上海总仓', mode: '干线', leadTime: 3 },
    '3_5': { name: '华南工厂 → 南京仓', mode: '干线', leadTime: 3 },
    '4_6': { name: '上海总仓 → 苏州配送站', mode: '支线', leadTime: 1 },
    '4_7': { name: '上海总仓 → 杭州配送站', mode: '支线', leadTime: 2 },
    '5_7': { name: '南京仓 → 杭州配送站', mode: '支线', leadTime: 2 },
    '5_8': { name: '南京仓 → 宁波配送站', mode: '支线', leadTime: 2 },
    '6_9': { name: '苏州配送站 → 上海门店', mode: '末端配送', leadTime: 1 },
    '6_10': { name: '苏州配送站 → 杭州门店', mode: '末端配送', leadTime: 2 },
    '7_9': { name: '杭州配送站 → 上海门店', mode: '末端配送', leadTime: 2 },
    '7_10': { name: '杭州配送站 → 杭州门店', mode: '末端配送', leadTime: 1 },
    '8_10': { name: '宁波配送站 → 杭州门店', mode: '末端配送', leadTime: 2 },
    '8_11': { name: '宁波配送站 → 宁波门店', mode: '末端配送', leadTime: 1 },
    '9_12': { name: '上海门店 → 交付需求', mode: '需求', leadTime: 0 },
    '10_12': { name: '杭州门店 → 交付需求', mode: '需求', leadTime: 0 },
    '11_12': { name: '宁波门店 → 交付需求', mode: '需求', leadTime: 0 },
  },
}

export function edgeKeyOf(u, v) {
  return `${u}_${v}`
}

export function supplyNodeMeta(id, meta) {
  return (meta && meta.nodes && meta.nodes[id]) || null
}

export function supplyEdgeMeta(u, v, meta) {
  return (meta && meta.edges && meta.edges[edgeKeyOf(u, v)]) || null
}

export function supplyNodeName(id, meta) {
  const node = supplyNodeMeta(id, meta)
  return node ? node.name || node.short || `节点 ${id}` : `节点 ${id}`
}

export function supplyNodeShort(id, meta) {
  const node = supplyNodeMeta(id, meta)
  return node ? node.short || node.name || String(id) : String(id)
}

export function supplyEdgeName(u, v, meta) {
  const edge = supplyEdgeMeta(u, v, meta)
  return edge ? edge.name || `${u} → ${v}` : `${supplyNodeShort(u, meta)} → ${supplyNodeShort(v, meta)}`
}

export function formatSupplyRoute(path, meta) {
  if (!Array.isArray(path)) return ''
  return path.map((id) => supplyNodeShort(id, meta)).join(' → ')
}

export function supplyCapacityOf(graph, u, v) {
  if (!graph) return 0
  if (!graph.directed && v < u) return supplyCapacityOf(graph, v, u)
  const edge = graph.edges.find((e) => e.u === u && e.v === v)
  return edge ? edge.cap : 0
}

export function buildGraphTextWithExtraCapacity(graph, u, v, delta) {
  if (!graph) return ''
  const lines = [`${graph.n} ${graph.edges.length} ${graph.directed ? 1 : 0}`]
  for (const e of graph.edges) {
    const cap = e.u === u && e.v === v ? e.cap + delta : e.cap
    lines.push(`${e.u} ${e.v} ${cap}`)
  }
  return `${lines.join('\n')}\n`
}

export function buildSupplyChainSummary(result, graph, meta) {
  if (!result || !graph || !meta) return null
  const sourceName = supplyNodeName(meta.source, meta)
  const sinkName = supplyNodeName(meta.sink, meta)
  const flowMap = new Map(
    (result.edgeFlow || []).map((e) => [edgeKeyOf(e.u, e.v), Math.max(0, e.flow)]),
  )

  const cutEdges = (result.cutEdges || []).map(([u, v]) => ({
    key: edgeKeyOf(u, v),
    u,
    v,
    cap: supplyCapacityOf(graph, u, v),
    label: supplyEdgeName(u, v, meta),
  }))

  const topEdges = graph.edges
    .map((e) => {
      const flow = flowMap.get(edgeKeyOf(e.u, e.v)) || 0
      const utilization = e.cap > 0 ? flow / e.cap : 0
      return {
        key: edgeKeyOf(e.u, e.v),
        u: e.u,
        v: e.v,
        cap: e.cap,
        flow,
        utilization,
        label: supplyEdgeName(e.u, e.v, meta),
      }
    })
    .filter((e) => e.flow > 0)
    .sort((a, b) => b.utilization - a.utilization || b.flow - a.flow)
    .slice(0, 6)

  const sourceOutCapacity = graph.edges
    .filter((e) => e.u === meta.source || (!graph.directed && e.v === meta.source))
    .reduce((sum, e) => sum + e.cap, 0)
  const demandTotal = meta.totalDemand || 0
  const demandSatisfied = demandTotal > 0 ? Math.min(result.value, demandTotal) : null

  return {
    maxFlow: result.value,
    cutCapacity: result.cutCapacity,
    correct: !!result.correct,
    sourceName,
    sinkName,
    unit: meta.unit || '单位',
    sourceOutCapacity,
    utilization: sourceOutCapacity > 0 ? result.value / sourceOutCapacity : null,
    demandTotal,
    demandSatisfied,
    demandRate: demandTotal > 0 ? demandSatisfied / demandTotal : null,
    cutEdges,
    topEdges,
  }
}

export function supplyChainSentence(frame, meta) {
  if (!frame) return ''
  switch (frame.kind) {
    case 'init':
      return '初始化供应链网络：每条运输线路先按容量设定“最多还能运多少货”。'
    case 'route':
      return `找到第 ${frame.round} 条补货路径：${formatSupplyRoute(frame.path, meta)}，这条路径本次最多增加 ${frame.bottleneck}${meta?.unit || '单位'}。`
    case 'push':
      if (!frame.balance) return `沿当前补货路径推进 ${frame.bottleneck}${meta?.unit || '单位'}。`
      return `线路「${supplyEdgeName(frame.balance.u, frame.balance.v, meta)}」余额 ${frame.balance.before} → ${frame.balance.after}，本次承担 ${frame.balance.delta}${meta?.unit || '单位'}。`
    case 'noPath':
      return `已经没有还能继续补货的线路，当前最大可交付量为 ${frame.totals.maxFlow}${meta?.unit || '单位'}。`
    case 'cut':
      return `瓶颈割集：${frame.cutEdges.map(([u, v]) => supplyEdgeName(u, v, meta)).join('、')}；这些线路的总容量 ${frame.totals.cutCapacity} 等于最大可交付量。`
    default:
      return frame.text || ''
  }
}

// ============================================================
//  问题定位：线路影响分析
//
//  思路：把“哪条线路出了问题、影响多大”变成一个能算的问题 ——
//  逐条线路做反事实模拟：只改这一条线（中断 / 运力减半 / 扩容），
//  重新算一次最大流，和基线对比，差值就是这条线路的影响力。
//
//  为什么这么算能说明问题：
//    * 最大流 = 最小割容量。不在任何最小割里的边，加大容量不会改变最大流，
//      所以“扩容只有瓶颈线路有效”是定理的推论，不是巧合；
//    * 但“删掉一条边”可能切出新路径，任何边中断都可能降低最大流，
//      所以“最脆弱”必须单独模拟，不能从瓶颈线路推出来。
//
//  安全约定：整个分析在“影子图”上做（core.loadText 改的是核心里的图），
//  结束后一定要把原始图文本还原回去（try/finally），
//  否则存储结构、遍历、最大流这些功能会全部基于被改坏的图。
// ============================================================

// 判定阈值：影响率 = 影响 / 基线。按经验分级，改这里就能调整口径。
export const IMPACT_THRESHOLDS = {
  fatal: 0.4, // ≥ 40% 记为“致命”
  sensitive: 0.1, // 10% ~ 40% 记为“敏感”
}

// 最多分析多少条线路：每条线路都要跑一次最大流，边太多会很慢
export const ANALYSIS_LIMIT = 300

// 界面上的三种情景
export const ANALYSIS_MODES = [
  { id: 'fail', label: '中断' },
  { id: 'degrade', label: '运力降级' },
  { id: 'expand', label: '扩容' },
]

// 情景参数的取值范围与默认值（界面上可以改）
export const DEGRADE_RANGE = { min: 5, max: 95, step: 5, default: 50 } // 降级到百分之多少
export const EXPAND_RANGE = { min: 1, max: 999, step: 1, default: 5 } // 扩容加多少

const LEVEL_TAGS = {
  fatal: '致命',
  sensitive: '敏感',
  minor: '轻微',
  none: '冗余',
  gain: '建议扩容',
  gainNone: '扩容无效',
}

/**
 * 按 edits 改一张图的文本（buildGraphTextWithExtraCapacity 的泛化版）。
 *
 * edits 里 cap === null 表示删掉这条边（模拟线路中断）——
 * 不能把容量写成 0，因为文件解析会把 ≤ 0 的容量规范成 1。
 * 第一行 "n m directed" 必须拼在同一行，否则核心会判定格式非法。
 */
export function buildGraphTextWithEdits(graph, edits) {
  if (!graph) return ''
  const drop = new Set()
  const change = new Map()
  for (const e of edits || []) {
    const key = edgeKeyOf(e.u, e.v)
    if (e.cap == null) drop.add(key)
    else change.set(key, Math.max(1, Math.floor(e.cap)))
  }

  const kept = graph.edges.filter((e) => !drop.has(edgeKeyOf(e.u, e.v)))
  const lines = [`${graph.n} ${kept.length} ${graph.directed ? 1 : 0}`]
  for (const e of kept) {
    const key = edgeKeyOf(e.u, e.v)
    const cap = change.has(key) ? change.get(key) : e.cap
    lines.push(`${e.u} ${e.v} ${cap}`)
  }
  return `${lines.join('\n')}\n`
}

function judgeLevel(mode, direction, rate) {
  if (mode === 'expand') return direction === 'gain' ? 'gain' : 'gainNone'
  if (direction !== 'loss') return 'none'
  if (rate >= IMPACT_THRESHOLDS.fatal) return 'fatal'
  if (rate >= IMPACT_THRESHOLDS.sensitive) return 'sensitive'
  return 'minor'
}

/**
 * 逐条线路做影响分析（在影子图上模拟，结束后还原核心里的图）。
 *
 * @param {object} core   算法接口（coreRef() 的返回值）
 * @param {object} graph  当前图快照（graph.edges 形如 [{u,v,cap}]）
 * @param {object} meta   供应链元数据（取线路名、需求总量、单位）
 * @param {object} options
 *   originalText 原始图文本（调用方先 await exportGraphText() 拿到，用于还原）
 *   mode         'fail' 中断 | 'degrade' 运力降级 | 'expand' 扩容
 *   delta        扩容增量（mode = 'expand' 时生效）
 *   ratio        降级比例 0~1（mode = 'degrade' 时生效，0.5 就是"运力减半"）
 *   source/sink  当前源点 / 汇点
 *   limit        最多分析多少条边（默认 ANALYSIS_LIMIT）
 * @returns 见 docs/供应链影响分析-设计说明.md
 */
export function analyzeLineImpact(core, graph, meta, options = {}) {
  const {
    originalText = '',
    mode = 'fail',
    delta = 5,
    ratio = 0.5,
    source = 1,
    sink = 1,
    limit = ANALYSIS_LIMIT,
  } = options

  const fail = (error) => ({
    ok: false,
    error,
    mode,
    delta,
    ratio,
    base: 0,
    demand: 0,
    gap: 0,
    unit: '单位',
    cutKeys: [],
    rows: [],
    summary: null,
    truncated: false,
  })

  if (!core) return fail('算法核心还没加载好')
  if (!graph || !graph.edges || !graph.edges.length) return fail('当前图没有边，无法做线路分析')
  if (!originalText) return fail('拿不到当前图的文本，无法在影子上做模拟')
  if (source === sink) return fail('源点和汇点不能是同一个点')

  const baseRes = core.maxFlow(source, sink)
  if (!baseRes.ok) return fail(baseRes.error || '基线最大流计算失败')

  const base = baseRes.value
  const cutKeys = new Set((baseRes.cutEdges || []).map(([u, v]) => edgeKeyOf(u, v)))
  const demand = Number(meta && meta.totalDemand) || 0
  const unitDelta = Math.max(1, Math.floor(delta))
  // 降级比例收在 1% ~ 99%：100% 等于没改，0% 就等价于"中断"了
  const unitRatio = Math.min(0.99, Math.max(0.01, Number(ratio) || 0.5))

  const edges = graph.edges.slice(0, Math.max(1, limit))
  const truncated = graph.edges.length > edges.length
  const rows = []

  try {
    for (const e of edges) {
      let cap = null // null = 删掉这条边（模拟中断）
      if (mode === 'degrade') cap = Math.max(1, Math.floor(e.cap * unitRatio))
      else if (mode === 'expand') cap = e.cap + unitDelta

      const text = buildGraphTextWithEdits(graph, [{ u: e.u, v: e.v, cap }])
      if (!core.loadText(text, 'analysis').ok) continue
      const res = core.maxFlow(source, sink)
      if (!res.ok) continue

      const diff = mode === 'expand' ? res.value - base : base - res.value
      const direction = diff > 0 ? (mode === 'expand' ? 'gain' : 'loss') : 'none'
      const impact = Math.abs(diff)
      const rate = base > 0 ? impact / base : 0
      const isCut = cutKeys.has(edgeKeyOf(e.u, e.v))
      const level = judgeLevel(mode, direction, rate)

      rows.push({
        key: edgeKeyOf(e.u, e.v),
        u: e.u,
        v: e.v,
        cap: e.cap,
        label: supplyEdgeName(e.u, e.v, meta),
        after: res.value,
        impact,
        rate,
        direction,
        isCut,
        level,
        tag: [LEVEL_TAGS[level], isCut ? '瓶颈' : ''].filter(Boolean).join(' · '),
      })
    }
  } finally {
    // 一定要还原：整个分析是在核心的“影子图”上做的
    core.loadText(originalText, 'restore')
  }

  const sorted = rows.slice().sort((a, b) => b.impact - a.impact || a.cap - b.cap)
  const affected = sorted.filter((r) => r.impact > 0)
  const summary = {
    worst: affected[0] || null,
    affected,
    redundant: rows.filter((r) => r.impact === 0),
    fatalCount: rows.filter((r) => r.level === 'fatal').length,
    sensitiveCount: rows.filter((r) => r.level === 'sensitive').length,
    redundantCount: rows.filter((r) => r.level === 'none').length,
  }

  return {
    ok: true,
    mode,
    delta: unitDelta,
    ratio: unitRatio,
    base,
    demand,
    gap: Math.max(0, demand - base),
    unit: (meta && meta.unit) || '单位',
    cutKeys: [...cutKeys],
    rows: sorted,
    summary,
    truncated,
  }
}
