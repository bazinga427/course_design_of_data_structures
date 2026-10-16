<template>
  <div class="stage-card panel">
    <div class="panel-head" v-if="!bare">
      <h3>图结构可视化</h3>
      <div class="row tight">
        <span class="hint" v-if="graph">鼠标滚轮缩放 · 拖动节点可以挪位置 · 点节点查看邻接表</span>
        <span class="hint" v-else>还没有图</span>
      </div>
    </div>

    <div class="toolbar" v-if="!bare">
      <div class="row tight">
        <span class="hint">布局</span>
        <button
          v-for="l in layouts"
          :key="l.id"
          class="btn tiny"
          :class="{ active: layoutId === l.id }"
          @click="applyLayout(l.id)"
        >
          {{ l.label }}
        </button>
      </div>
      <div class="row tight">
        <span class="hint">点大小</span>
        <button
          v-for="s in nodeSizePresets"
          :key="s.px"
          class="btn tiny"
          :class="{ active: nodeSize === s.px }"
          @click="setNodeSize(s.px)"
        >
          {{ s.label }}
        </button>
      </div>
      <div class="row tight">
        <button class="btn tiny" @click="fit">适应窗口</button>
        <button class="btn tiny" @click="zoomBy(1.2)">放大</button>
        <button class="btn tiny" @click="zoomBy(1 / 1.2)">缩小</button>
        <button class="btn tiny" :class="{ active: showEdgeLabels }" @click="toggleLabels">
          边权标签
        </button>
        <button class="btn tiny" :class="{ active: showResidual }" @click="toggleResidual">
          残量网络
        </button>
        <button class="btn tiny" :class="{ active: showReverseArcs }" @click="toggleReverseArcs">
          反向边
        </button>
        <button class="btn tiny" @click="exportPng" :disabled="!graph">导出 PNG</button>
      </div>
    </div>

    <div class="canvas-host" ref="host"></div>

    <div v-if="!graph" class="canvas-empty">
      <p>用顶部「打开图文件 / 载入示例 / 随机生成」建一张图</p>
      <p class="hint">示例图直接点「载入示例」，自带 graph.txt、graph2.txt、rev.txt</p>
    </div>

    <div class="canvas-legend">
      <span class="legend"><b class="mono">余x/总y</b> = 余额 / 权重（工具栏「残量网络」可切回只显示容量）</span>
      <span class="legend"><i style="background: #0e7490"></i>已访问</span>
      <span class="legend"><i style="background: #fbbf24"></i>当前点</span>
      <span class="legend"><i style="background: #22d3ee"></i>遍历树边</span>
      <span class="legend"><i style="background: #f87171"></i>增广路</span>
      <span class="legend"><i style="background: #34d399"></i>正在推流 / 有流量</span>
      <span class="legend"><i style="background: #38bdf8"></i>反向弧（余额 = 可撤回的流）</span>
      <span class="legend"><i style="background: #f472b6"></i>割边</span>
      <span class="legend"><i style="background: #475569"></i>余额 0（被擦掉）</span>
      <span class="legend"><i style="background: #60a5fa"></i>S 侧 / 源点</span>
      <span class="legend"><i style="background: #a78bfa"></i>T 侧 / 汇点</span>
    </div>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref, shallowRef, watch } from 'vue'
import cytoscape from 'cytoscape'
import { store, notify, edgeKey, reverseEdgeKey } from '../store/graphStore.js'

// bare = 演示模式下只要画布，不要面板标题和工具栏
defineProps({ bare: { type: Boolean, default: false } })
const host = ref(null)
const cy = shallowRef(null)
const layoutId = ref('force')
const showEdgeLabels = ref(true)
// 反向弧（残余网络里用来撤回流量的弧）默认显示，工具栏可以关掉
const showReverseArcs = ref(true)
// 边上的数字是「余 x / 总 y」（残量网络视角）还是原图的容量（原图视角）。
// 放在 store 里是因为最大流面板也在算边标签，两边必须用同一个口径。
const showResidual = computed({
  get: () => store.showResidual,
  set: (v) => {
    store.showResidual = v
  },
})
// 点在屏幕上画多大（像素）。这个是屏幕像素，不是“模型坐标”，
// 所以换布局、缩放都不会变；工具栏里的「点大小」就是改这个值。
const nodeSize = ref(28)
const nodeSizePresets = [
  { px: 20, label: '小' },
  { px: 28, label: '中' },
  { px: 38, label: '大' },
]

const graph = computed(() => store.graph)
const supplyMeta = computed(() => store.supplyChainMeta)

const layouts = [
  { id: 'force', label: '力导向' },
  { id: 'circle', label: '环形' },
  { id: 'grid', label: '网格' },
  { id: 'breadthfirst', label: '层次' },
  { id: 'random', label: '随机' },
]

const nodeId = (id) => `n${id}`

// 两个图层叠起来：先遍历后最大流，所以最大流的高亮更“优先”
const mergedHighlight = computed(() => {
  const nodes = {}
  const edges = {}
  const edgeLabels = {}
  const nodeLabels = {}
  for (const layer of [store.traversalLayer, store.flowLayer]) {
    if (!layer) continue
    Object.assign(nodes, layer.nodes || {})
    Object.assign(edges, layer.edges || {})
    Object.assign(edgeLabels, layer.edgeLabels || {})
    Object.assign(nodeLabels, layer.nodeLabels || {})
  }
  return { nodes, edges, edgeLabels, nodeLabels }
})

// ------------------------------------------------------------
//  样式表
//
//  Cytoscape 里 width / height / font-size / border-width 的单位是
//  「模型坐标」：它们会跟着画布缩放一起放大缩小。所以同一张图换个布局
//  （每个布局的包围盒不一样，fit 之后的缩放比例也不一样）点看起来就
//  时大时小 —— 力导向布局里点显得比别的布局大，就是这里来的。
//
//  这里的做法：所有和“看起来多大”有关的数都乘上 1/当前缩放，按屏幕
//  像素来算。于是不管用哪个布局、缩放到多少，点的直径始终是
//  nodeSize 像素（默认 28px，工具栏「点大小」可以调）。
// ------------------------------------------------------------
function buildStylesheet() {
  const zoom = cy.value && cy.value.zoom() > 0 ? cy.value.zoom() : 1
  const u = 1 / zoom // 1 个屏幕像素等于多少模型坐标
  const d = nodeSize.value // 点的直径（屏幕像素）
  // 有向边的箭头大小 = 边宽 × arrow-scale（见 cytoscape 的 getArrowWidth），
  // 边宽已经是屏幕像素了，所以箭头也是屏幕像素大小；再让它跟着点大小走，
  // 点调大箭头大一点、点调小箭头小一点，看起来才协调。
  const arrow = 0.45 * (d / 28)
  return [
  {
    selector: 'node',
    style: {
      width: d * u,
      height: d * u,
      label: 'data(label)',
      color: '#dbe6f5',
      'font-size': 12 * u,
      'font-weight': 600,
      'text-valign': 'center',
      'text-halign': 'center',
      'text-wrap': 'wrap',
      'text-max-width': d * 2.1 * u,
      'background-color': '#1b2942',
      'border-width': 2 * u,
      'border-color': '#3a4d70',
      'transition-property': 'background-color, border-color, width, height, opacity',
      'transition-duration': '180ms',
    },
  },
  {
    selector: 'node.sc-supplier',
    style: { 'background-color': '#0369a1', 'border-color': '#7dd3fc' },
  },
  {
    selector: 'node.sc-factory',
    style: { 'background-color': '#0e7490', 'border-color': '#67e8f9' },
  },
  {
    selector: 'node.sc-warehouse',
    style: { 'background-color': '#1d4ed8', 'border-color': '#93c5fd' },
  },
  {
    selector: 'node.sc-regional',
    style: { 'background-color': '#4f46e5', 'border-color': '#a5b4fc' },
  },
  {
    selector: 'node.sc-dc',
    style: { 'background-color': '#6d28d9', 'border-color': '#c4b5fd' },
  },
  {
    selector: 'node.sc-store',
    style: { 'background-color': '#047857', 'border-color': '#6ee7b7' },
  },
  {
    selector: 'node.sc-demand',
    style: { 'background-color': '#be185d', 'border-color': '#f9a8d4' },
  },
  {
    selector: 'node.setS',
    style: { 'background-color': '#1d4ed8', 'border-color': '#93c5fd' },
  },
  {
    selector: 'node.setT',
    style: { 'background-color': '#6d28d9', 'border-color': '#c4b5fd' },
  },
  {
    selector: 'node.source',
    style: {
      'background-color': '#1d4ed8',
      'border-color': '#bfdbfe',
      'border-width': 4 * u,
    },
  },
  {
    selector: 'node.sink',
    style: {
      'background-color': '#7c3aed',
      'border-color': '#ddd6fe',
      'border-width': 4 * u,
    },
  },
  {
    selector: 'node.visited',
    style: { 'background-color': '#0e7490', 'border-color': '#22d3ee' },
  },
  {
    selector: 'node.frontier',
    style: { 'background-color': '#155e75', 'border-color': '#67e8f9' },
  },
  {
    selector: 'node.current',
    style: {
      'background-color': '#f59e0b',
      'border-color': '#fde68a',
      color: '#2a1a00',
      width: d * 1.3 * u,
      height: d * 1.3 * u,
      'border-width': 4 * u,
      'z-index': 20,
    },
  },
  {
    selector: 'node.selected',
    style: { 'border-color': '#ffffff', 'border-width': 3 * u },
  },
  {
    selector: 'edge',
    style: {
      width: 2 * u,
      'line-color': '#3d5175',
      'curve-style': 'bezier',
      label: 'data(label)',
      color: '#a8bcd8',
      'font-size': 10.5 * u,
      'text-rotation': 'autorotate',
      'text-background-color': '#0a1120',
      'text-background-opacity': 0.85,
      'text-background-padding': 1.5 * u,
      'text-border-opacity': 0,
      'target-arrow-shape': 'none',
      'transition-property': 'line-color, width, opacity',
      'transition-duration': '180ms',
    },
  },
  {
    selector: 'edge.directed',
    style: {
      'target-arrow-shape': 'triangle',
      'target-arrow-color': '#3d5175',
      'arrow-scale': arrow,
    },
  },
  {
    // 反向弧：原图里没有这条弧，但正方向推过流之后，残余网络里它就有余额了，
    // 表示这些流量可以撤回来。没有余额时不画（由 applyHighlight 控制显示）。
    selector: 'edge.reverseArc',
    style: {
      display: 'none',
      'line-color': '#38bdf8',
      'target-arrow-color': '#38bdf8',
      'line-style': 'dashed',
      width: 2.5 * u,
      'arrow-scale': 0.34 * (d / 28),
      'curve-style': 'unbundled-bezier',
      'control-point-distances': -26 * u,
      'control-point-weights': 0.5,
      'z-index': 22,
    },
  },

  {
    selector: 'edge.tree',
    style: {
      'line-color': '#22d3ee',
      'target-arrow-color': '#22d3ee',
      width: 3.5 * u,
      'z-index': 10,
    },
  },
  {
    selector: 'edge.path',
    style: {
      'line-color': '#f87171',
      'target-arrow-color': '#f87171',
      width: 4.5 * u,
      'z-index': 30,
    },
  },
  {
    selector: 'edge.flowEdge',
    style: { 'line-color': '#34d399', 'target-arrow-color': '#34d399' },
  },
  {
    // 正在试探的弧：黄色实线
    selector: 'edge.probe',
    style: {
      'line-color': '#f59e0b',
      'target-arrow-color': '#f59e0b',
      width: 4 * u,
      'z-index': 25,
    },
  },
  {
    // 走不通、要回退的弧：灰色虚线
    selector: 'edge.deadend',
    style: {
      'line-color': '#6b7280',
      'target-arrow-color': '#6b7280',
      width: 3 * u,
      'line-style': 'dashed',
      'z-index': 18,
    },
  },
  {
    // 本步残余容量刚更新过的弧：绿色加粗
    selector: 'edge.update',
    style: {
      'line-color': '#34d399',
      'target-arrow-color': '#34d399',
      width: 4.5 * u,
      'z-index': 28,
    },
  },
  {
    // 「整理残余网络」那一步被擦掉的弧：余额已经是 0，画成灰色虚线
    selector: 'edge.dropped',
    style: {
      'line-color': '#475569',
      'target-arrow-color': '#475569',
      'line-style': 'dashed',
      width: 1.6 * u,
      opacity: 0.45,
      'z-index': 4,
    },
  },
  {
    selector: 'edge.cut',
    style: {
      'line-color': '#f472b6',
      'target-arrow-color': '#f472b6',
      width: 5 * u,
      'line-style': 'dashed',
      'z-index': 40,
    },
  },
  ]
}

function buildElements() {
  const g = store.graph
  if (!g) return []
  const elements = g.nodes.map((id) => ({
    data: { id: nodeId(id), nodeId: id, label: String(id) },
  }))
  const listed = new Set(g.edges.map((e) => `${e.u}_${e.v}`))
  for (const e of g.edges) {
    elements.push({
      data: {
        id: `e${e.u}_${e.v}`,
        source: nodeId(e.u),
        target: nodeId(e.v),
        u: e.u,
        v: e.v,
        cap: e.cap,
        label: String(e.cap),
      },
      classes: g.directed ? 'directed' : '',
    })
  }

  // 再把反向弧也建出来（比如 1→2 的反向弧 2→1）。它们平时不显示，
  // 只有正方向推过流、残余网络里真有余额时才画出来。图太大就不铺了。
  if (g.edges.length <= 400) {
    for (const e of g.edges) {
      if (listed.has(`${e.v}_${e.u}`)) continue // 反方向的弧本来就在图里
      elements.push({
        data: {
          id: `r${e.u}_${e.v}`,
          source: nodeId(e.v),
          target: nodeId(e.u),
          u: e.v,
          v: e.u,
          rev: true,
          cap: 0,
          label: '',
        },
        classes: 'reverseArc directed',
      })
    }
  }
  return elements
}

function layoutOptions(id) {
  const directed = !!(store.graph && store.graph.directed)
  const common = { animate: true, animationDuration: 420, padding: 46, fit: true }
  switch (id) {
    case 'circle':
      return { name: 'circle', ...common, spacingFactor: 1.1 }
    case 'grid':
      return { name: 'grid', ...common }
    case 'breadthfirst':
      return { name: 'breadthfirst', directed, spacingFactor: 1.25, ...common }
    case 'random':
      return { name: 'random', ...common }
    case 'force':
    default:
      return {
        name: 'cose',
        ...common,
        animationDuration: 600,
        nodeRepulsion: 9000,
        idealEdgeLength: 95,
        edgeElasticity: 90,
        nestingFactor: 1.1,
        gravity: 70,
        numIter: 800,
        initialTemp: 220,
        coolingFactor: 0.95,
        randomize: true,
      }
  }
}

// 把当前缩放下的样式表换成新的：所有尺寸都按屏幕像素重新算一遍
function restyle() {
  const c = cy.value
  if (!c) return
  c.style(buildStylesheet())
}

// 布局跑完 / 改了「点大小」之后走这里：
//   1) 先把点换算成屏幕像素大小
//   2) 再适应一次窗口（点的模型尺寸变了，原来那次 fit 的边距就不够用了）
//   3) fit 会改缩放，所以最后再换算一次
// 注意 cytoscape 的 fit(elements, padding) 里 padding 的单位是屏幕像素，
// 不跟着缩放走，所以这里直接写像素数就行。
function scaleAndFit(marginPx = 46) {
  const c = cy.value
  if (!c || !c.nodes().length) return
  restyle()
  c.fit(undefined, marginPx)
  restyle()
}

function applyLayout(id = layoutId.value) {
  const c = cy.value
  if (!c || c.nodes().length === 0) return
  layoutId.value = id
  // 点数很多的时候力导向布局非常慢，自动退化成环形
  const tooMany = c.nodes().length > 260
  const use = id === 'force' && tooMany ? 'circle' : id
  if (use !== id) notify('点数较多，力导向布局已自动换成环形布局', 'warn')
  const lay = c.layout(layoutOptions(use))
  // 布局自己的 fit 是在“旧的点尺寸”下算出来的，等它停下来再按屏幕像素校正
  lay.one('layoutstop', () => scaleAndFit())
  lay.run()
}

function setNodeSize(px) {
  nodeSize.value = px
  scaleAndFit()
}

function applyHighlight() {
  const c = cy.value
  if (!c) return
  const h = mergedHighlight.value
  const g = store.graph
  c.batch(() => {
    c.elements().removeClass(
      'visited current frontier tree path cut dropped flowEdge probe deadend update setS setT source sink sc-supplier sc-factory sc-warehouse sc-regional sc-dc sc-store sc-demand',
    )

    c.nodes().forEach((node) => {
      const id = node.data('nodeId')
      const cls = h.nodes[id]
      if (cls) node.addClass(cls)
      // 源点 / 汇点始终套一圈，方便一眼看出来
      if (g && g.n >= 2) {
        if (id === store.source) node.addClass('source')
        if (id === store.sink) node.addClass('sink')
      }
      const metaNode = supplyMeta.value && supplyMeta.value.nodes ? supplyMeta.value.nodes[id] : null
      if (metaNode && metaNode.type) node.addClass(`sc-${metaNode.type}`)
      const hasLayerLabel = Object.prototype.hasOwnProperty.call(h.nodeLabels, id)
      const label = hasLayerLabel
        ? h.nodeLabels[id]
        : metaNode ? (metaNode.short || metaNode.name || String(id)) : String(id)
      node.data('label', label)
    })

    c.edges().forEach((edge) => {
      const isRev = edge.data('rev') === true
      const key = isRev
        ? reverseEdgeKey(edge.data('u'), edge.data('v'))
        : edgeKey(edge.data('u'), edge.data('v'))
      const cls = h.edges[key]
      const custom = h.edgeLabels[key]

      if (isRev) {
        // 反向弧只有“有余额”时才画出来，也就是正方向真的推过流
        const visible = showReverseArcs.value && (cls !== undefined || custom !== undefined)
        edge.style('display', visible ? 'element' : 'none')
        if (!visible) return
      }

      if (cls) edge.addClass(cls)
      if (!isRev) {
        edge.removeStyle('line-fill line-gradient-stop-positions line-gradient-stop-colors')
      }
      if (!showEdgeLabels.value) edge.data('label', '')
      else if (custom !== undefined) edge.data('label', custom)
      else if (!isRev) {
        // 图层没给数字（还没算过最大流）时：残量网络的余额就是容量本身
        const cap = edge.data('cap')
        edge.data('label', showResidual.value ? `余${cap}/总${cap}` : String(cap))
      }
    })
  })
}

function syncSelection() {
  const c = cy.value
  if (!c) return
  c.nodes().unselect()
  if (store.selectedNode != null) {
    const node = c.getElementById(nodeId(store.selectedNode))
    if (node && node.length) node.select()
  }
}

function rebuild() {
  const c = cy.value
  if (!c) return
  c.elements().remove()
  const elements = buildElements()
  if (elements.length) {
    c.add(elements)
    applyLayout()
  }
  applyHighlight()
  syncSelection()
}

function fit() {
  scaleAndFit()
}

function zoomBy(factor) {
  const c = cy.value
  if (!c || !c.nodes().length) return
  c.zoom({
    level: Math.min(4, Math.max(0.15, c.zoom() * factor)),
    renderedPosition: { x: c.width() / 2, y: c.height() / 2 },
  })
}

function toggleLabels() {
  showEdgeLabels.value = !showEdgeLabels.value
  applyHighlight()
}

function toggleReverseArcs() {
  showReverseArcs.value = !showReverseArcs.value
  applyHighlight()
}

// 残量网络 / 原图：只换边上数字的口径，反向弧由「反向边」按钮单独控制
function toggleResidual() {
  showResidual.value = !showResidual.value
  applyHighlight()
}

function exportPng() {
  const c = cy.value
  if (!c || !store.graph) return
  const uri = c.png({ full: true, scale: 2, bg: '#0b1220' })
  const link = document.createElement('a')
  link.href = uri
  link.download = `${(store.fileName || 'graph').replace(/[\\/:*?"<>|]/g, '_')}.png`
  link.click()
  notify('已导出 PNG 图片', 'success')
}

let resizeObserver = null

onMounted(() => {
  cy.value = cytoscape({
    container: host.value,
    elements: [],
    style: buildStylesheet(),
    wheelSensitivity: 0.22,
    minZoom: 0.15,
    maxZoom: 4,
    boxSelectionEnabled: false,
    selectionType: 'single',
  })

  cy.value.on('tap', 'node', (event) => {
    store.selectedNode = event.target.data('nodeId')
  })
  cy.value.on('tap', (event) => {
    if (event.target === cy.value) store.selectedNode = null
  })

  resizeObserver = new ResizeObserver(() => cy.value && cy.value.resize())
  if (host.value) resizeObserver.observe(host.value)

  rebuild()
})

onBeforeUnmount(() => {
  if (resizeObserver) resizeObserver.disconnect()
  if (cy.value) cy.value.destroy()
})

watch(() => store.graph, rebuild)
watch(mergedHighlight, applyHighlight, { deep: true })
watch(showEdgeLabels, applyHighlight)
watch(showReverseArcs, applyHighlight)
watch(() => store.showResidual, applyHighlight)
watch(supplyMeta, applyHighlight)

watch(() => store.selectedNode, syncSelection)
watch(() => [store.source, store.sink], applyHighlight)
</script>

<style scoped>
.stage-card {
  height: 100%;
  display: flex;
  flex-direction: column;
  position: relative;
}
.toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  padding: 8px 12px;
  border-bottom: 1px solid var(--line-soft);
  background: #0e1727;
  flex-wrap: wrap;
}
.canvas-host {
  flex: 1;
  min-height: 380px;
  background:
    radial-gradient(900px 420px at 70% 0%, #101d31 0%, #0a1120 60%),
    #0a1120;
}
.canvas-empty {
  position: absolute;
  inset: 96px 0 40px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 6px;
  color: var(--text-dim);
  pointer-events: none;
}
.canvas-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  padding: 7px 12px;
  border-top: 1px solid var(--line-soft);
  background: #0e1727;
}
</style>
