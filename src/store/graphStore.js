// ============================================================
//  graphStore.js —— 全页面共用的状态
//
//  不引 pinia，用一个 reactive 对象就够了：
//    * 当前这张图（store.graph，来自 core 的 snapshot）
//    * 两个高亮图层：遍历面板写 traversalLayer，最大流面板写 flowLayer，
//      画布把它们叠起来画（flow 优先级更高）
//    * 顶部提示条 toast
// ============================================================

import { reactive } from 'vue'
import { loadGraphApi } from '../wasm/loader.js'

export function emptyLayer(kind = 'none') {
  return { kind, nodes: {}, edges: {}, edgeLabels: {}, nodeLabels: {}, title: '' }
}

// 一条边在“高亮图层”里的键。无向图 u-v 和 v-u 是同一条边，统一成小号在前。
export function edgeKey(u, v) {
  const directed = store.graph ? store.graph.directed : 1
  if (!directed && v < u) return `${v}_${u}`
  return `${u}_${v}`
}

export const store = reactive({
  backend: 'loading', // loading | wasm | js
  backendLabel: '正在加载算法核心…',
  coreInfo: null,

  fileName: '未命名图',
  graph: null, // { n, m, directed, nodes, edges, adjList, matrix, matrixOmitted }
  selectedNode: null,
  source: 1,
  sink: 1,

  traversalLayer: emptyLayer(),
  flowLayer: emptyLayer(),
  flowResult: null, // 最近一次最大流结果，邻接矩阵面板会顺便显示 flow/cap

  toast: { text: '', type: 'info', seq: 0 },
})

let core = null

export function coreRef() {
  return core
}

export function notify(text, type = 'info') {
  store.toast = { text, type, seq: store.toast.seq + 1 }
}

export async function initCore() {
  core = await loadGraphApi({ verbose: false })
  store.backend = core.backend
  store.backendLabel = core.backendLabel
  const info = core.version()
  store.coreInfo = info && info.ok ? info : null
  return core
}

function resetFocus() {
  store.traversalLayer = emptyLayer()
  store.flowLayer = emptyLayer()
  store.flowResult = null
}

async function refreshSnapshot() {
  const snap = core.snapshot()
  if (!snap.ok) {
    notify(snap.error, 'error')
    return false
  }
  store.graph = snap
  store.selectedNode = null
  store.source = snap.n >= 1 ? 1 : 0
  store.sink = snap.n >= 2 ? snap.n : snap.n
  resetFocus()
  return true
}

/** 用文件内容建图（读取文件恢复图） */
export async function loadGraphText(text, fileName = '未命名图') {
  if (!core) return false
  const res = core.loadText(text, fileName)
  if (!res.ok) {
    notify(res.error, 'error')
    return false
  }
  store.fileName = fileName
  if (!(await refreshSnapshot())) return false
  // base = 0 表示这份文件用的是 0 开始编号，核心已经帮我们 +1 映射好了
  const tip =
    res.base === 0
      ? `（文件用的是 0 开始编号，已自动映射成 1..${store.graph.n}）`
      : ''
  notify(
    `已载入 ${store.graph.n} 个点、${store.graph.edges.length} 条边${tip}`,
    'success',
  )
  return true
}

/** 随机生成一张图（创建随机图文件） */
export async function generateRandom({ n, m, directed, seed, maxCapacity }) {
  if (!core) return false
  const res = core.random(n, m, directed ? 1 : 0, seed, maxCapacity)
  if (!res.ok) {
    notify(res.error, 'error')
    return false
  }
  if (!(await refreshSnapshot())) return false
  store.fileName = `随机图 seed=${res.seed}`
  notify(
    `已生成随机图：${store.graph.n} 个点、${store.graph.edges.length} 条边（seed=${res.seed} 可复现）`,
    'success',
  )
  return true
}

/** 把当前图导出成 graph.txt 那样格式的文本 */
export async function exportGraphText() {
  if (!core) return null
  const res = core.saveText()
  if (!res.ok) {
    notify(res.error, 'error')
    return null
  }
  return res.text
}

export function setTraversalLayer(layer) {
  store.traversalLayer = layer || emptyLayer()
}

export function setFlowLayer(layer) {
  store.flowLayer = layer || emptyLayer()
}
