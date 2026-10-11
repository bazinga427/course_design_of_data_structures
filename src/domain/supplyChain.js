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