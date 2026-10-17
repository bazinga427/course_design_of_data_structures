<template>
  <div class="panel supply-panel">
    <div class="panel-head">
      <h3>物流与供应链</h3>
      <span class="hint">{{ active ? '场景已启用' : '最大运力瓶颈分析' }}</span>
    </div>

    <div class="panel-body">
      <div class="row">
        <button class="btn accent" @click="loadPreset">载入供应链示例</button>
        <button class="btn" :disabled="!active" @click="clearPreset">退出场景</button>
      </div>

      <p v-if="!active" class="hint">
        把当前有向图解释成供应链网络：源点=供应能力，汇点=交付需求，边容量=线路运力。
        载入示例后会按“工厂 → 仓库 → 配送站 → 门店”计算最大可交付量和瓶颈线路。
      </p>

      <template v-else>
        <div class="role-legend">
          <span v-for="r in roleLegend" :key="r.type" class="legend">
            <i :style="{ background: r.color }"></i>{{ r.label }}
          </span>
        </div>

        <div v-if="summary" class="stat-line">
          <div class="stat green"><span>最大可交付</span><b>{{ summary.maxFlow }}</b></div>
          <div class="stat violet"><span>瓶颈容量</span><b>{{ summary.cutCapacity }}</b></div>
          <div class="stat cyan">
            <span>需求满足率</span>
            <b>{{ percent(summary.demandRate) }}</b>
          </div>
        </div>

        <p v-else class="hint">还没有最大流结果，点左侧“计算最大流 / 最小割”或在载入示例后自动计算。</p>

        <div v-if="summary" class="biz-card">
          <b>{{ headline }}</b>
          <p>{{ explanation }}</p>
        </div>

        <template v-if="summary">
          <div class="mini-title">瓶颈线路</div>
          <div v-if="summary.cutEdges.length" class="cut-list">
            <div v-for="edge in summary.cutEdges" :key="edge.key" class="cut-row">
              <span>{{ edge.label }}</span>
              <b>{{ edge.cap }}</b>
            </div>
          </div>
          <p v-else class="hint">源点到交付需求当前不可达，或没有割边可分析。</p>

          <div class="mini-title">扩容模拟</div>
          <div class="row tight">
            <select v-model="selectedCutKey" :disabled="!summary.cutEdges.length">
              <option v-for="edge in summary.cutEdges" :key="edge.key" :value="edge.key">
                {{ edge.label }}（容量 {{ edge.cap }}）
              </option>
            </select>
            <input class="delta" type="number" min="1" v-model.number="delta" />
            <button class="btn tiny" :disabled="!selectedCut || simulating" @click="simulate">
              模拟
            </button>
          </div>
          <p v-if="simResult" class="hint ok">
            扩容后最大可交付 {{ simResult.from }} → {{ simResult.to }}，提升
            {{ simResult.gain }}（{{ simResult.edge.label }} +{{ simResult.delta }}）。
          </p>
          <p v-else class="hint">默认对选中的瓶颈线路增加容量，计算“再投入运力能提升多少”。</p>

          <div class="mini-title">关键线路利用率</div>
          <div class="util-list">
            <div v-for="edge in summary.topEdges" :key="edge.key" class="util-row">
              <span class="u-name">{{ edge.label }}</span>
              <span class="u-bar"><i :style="{ width: Math.round(edge.utilization * 100) + '%' }"></i></span>
              <b>{{ edge.flow }}/{{ edge.cap }}</b>
            </div>
          </div>
        </template>

        <!-- ---------- 问题定位：线路影响分析 ---------- -->
        <div class="mini-title analysis-title">
          问题定位 · 线路影响分析
          <span class="hint">改一条线重算一次，看影响多大</span>
        </div>

        <div class="row tight">
          <span class="hint">情景</span>
          <button
            v-for="m in ANALYSIS_MODES"
            :key="m.id"
            class="btn tiny"
            :class="{ active: analysisMode === m.id }"
            :disabled="analyzing"
            @click="setMode(m.id)"
          >
            {{ m.label }}
          </button>
        </div>

        <div class="row tight">
          <label class="field">
            <span>降级至</span>
            <input
              class="delta"
              type="number"
              :min="DEGRADE_RANGE.min"
              :max="DEGRADE_RANGE.max"
              :step="DEGRADE_RANGE.step"
              v-model.number="degradePercent"
              @change="onParamChange('degrade')"
            />
            <span class="hint">%</span>
          </label>
          <label class="field">
            <span>扩容 +</span>
            <input
              class="delta"
              type="number"
              :min="EXPAND_RANGE.min"
              :max="EXPAND_RANGE.max"
              :step="EXPAND_RANGE.step"
              v-model.number="analysisDelta"
              @change="onParamChange('expand')"
            />
          </label>
          <button class="btn tiny accent" :disabled="analyzing" @click="runAnalysis(true)">
            {{ analyzing ? '分析中…' : '重新分析' }}
          </button>
        </div>

        <p v-if="!analysis" class="hint">
          点上面的情景按钮或「重新分析」：逐条线路模拟中断 / 运力减半 / 扩容，
          找出最脆弱的线路和该投入的线路。
        </p>

        <template v-else>
          <div class="stat-line">
            <div class="stat green"><span>基线可交付</span><b>{{ analysis.base }}</b></div>
            <div class="stat violet"><span>需求缺口</span><b>{{ analysis.gap }}</b></div>
            <div class="stat cyan"><span>瓶颈线路</span><b>{{ analysis.cutKeys.length }}</b></div>
          </div>

          <div class="impact-list">
            <button
              v-for="row in analysisRows"
              :key="row.key"
              class="impact-row"
              :class="[row.level, { on: store.focusEdge === row.key }]"
              @click="focusLine(row)"
            >
              <span class="i-name">{{ row.label }}</span>
              <span class="i-tag">{{ row.tag }}</span>
              <span class="i-meta mono">容量 {{ row.cap }} → 情景后 {{ row.after }}</span>
              <span class="i-delta mono">{{ deltaText(row) }}</span>
            </button>
          </div>

          <div class="row tight">
            <button class="btn tiny" @click="expanded = !expanded">
              {{ expanded ? '只看有影响的线路' : `展开全部 ${analysis.rows.length} 条` }}
            </button>
            <span class="hint">点一行可以在画布上高亮这条线路</span>
          </div>

          <div class="biz-card">
            <b>{{ conclusion.title }}</b>
            <p v-for="(line, i) in conclusion.lines" :key="i">{{ line }}</p>
          </div>

          <p v-if="analysis.truncated" class="hint warn">
            线路较多，只分析了前 {{ analysis.rows.length }} 条（上限 {{ ANALYSIS_LIMIT }} 条）。
          </p>
        </template>
      </template>
    </div>
  </div>
</template>

<script setup>
import { computed, nextTick, ref, watch } from 'vue'
import {
  store,
  coreRef,
  loadGraphText,
  exportGraphText,
  notify,
  setSupplyChainMeta,
} from '../store/graphStore.js'
import {
  SUPPLY_CHAIN_SAMPLE,
  ANALYSIS_LIMIT,
  ANALYSIS_MODES,
  analyzeLineImpact,
  buildGraphTextWithExtraCapacity,
  buildSupplyChainSummary,
  supplyChainSentence,
  DEGRADE_RANGE,
  EXPAND_RANGE,
} from '../domain/supplyChain.js'

const delta = ref(5)
const selectedCutKey = ref('')
const simulating = ref(false)
const simResult = ref(null)

// ---- 问题定位：线路影响分析 ----
const analysisMode = ref('fail') // 'fail' | 'degrade' | 'expand'
const analysisDelta = ref(EXPAND_RANGE.default) // 扩容增量（可输入）
const degradePercent = ref(DEGRADE_RANGE.default) // 降级到百分之多少（可输入）
const analysis = ref(null) // analyzeLineImpact 的返回值
const analyzing = ref(false)
const expanded = ref(false) // 是否展开全部线路
const analysisCache = new Map() // key = 情景|增量|源|汇|图规模

const active = computed(() => !!store.supplyChainMeta)
const summary = computed(() =>
  buildSupplyChainSummary(store.flowResult, store.graph, store.supplyChainMeta),
)
const selectedCut = computed(() => {
  const edges = summary.value ? summary.value.cutEdges : []
  return edges.find((e) => e.key === selectedCutKey.value) || edges[0] || null
})
const headline = computed(() => {
  if (!summary.value) return '供应链网络已载入'
  return `最大可交付量 = ${summary.value.maxFlow}${summary.value.unit}，瓶颈线路 ${summary.value.cutEdges.length} 条`
})
const explanation = computed(() => {
  if (!summary.value) return ''
  return supplyChainSentence(store.flowFrame, store.supplyChainMeta) ||
    `最小割容量 ${summary.value.cutCapacity}，说明当前网络最多还能稳定交付 ${summary.value.maxFlow}${summary.value.unit}。`
})

const roleLegend = [
  { type: 'supplier', label: '供应能力', color: '#38bdf8' },
  { type: 'factory', label: '工厂', color: '#22d3ee' },
  { type: 'warehouse', label: '总仓', color: '#60a5fa' },
  { type: 'regional', label: '区域仓', color: '#818cf8' },
  { type: 'dc', label: '配送站', color: '#a78bfa' },
  { type: 'store', label: '门店', color: '#34d399' },
  { type: 'demand', label: '交付需求', color: '#f472b6' },
]

function percent(v) {
  if (v == null) return '—'
  return `${Math.round(v * 100)}%`
}

// 表格默认只显示有影响的线路，点「展开全部」才铺满
const analysisRows = computed(() => {
  const a = analysis.value
  if (!a) return []
  const rows = [...a.rows].sort((x, y) => y.impact - x.impact || x.cap - y.cap)
  return expanded.value ? rows : rows.filter((r) => r.impact > 0)
})

function deltaText(row) {
  if (!row || row.direction === 'none') return '0'
  const sign = row.direction === 'gain' ? '+' : '-'
  return `${sign}${row.impact}（${sign}${Math.round(row.rate * 100)}%）`
}

function labelList(rows) {
  const shown = rows.slice(0, 4).map((r) => `${r.label}（+${r.impact}）`)
  return rows.length > 4 ? `${shown.join('、')} 等 ${rows.length} 条` : shown.join('、')
}

// 情景的中文说法（带当前参数，用在结论里）
function scenarioPhrase(mode, a) {
  if (mode === 'fail') return '中断后可交付量'
  if (mode === 'degrade') return `运力降至 ${Math.round(((a && a.ratio) || 0.5) * 100)}% 后可交付量`
  return `扩容 +${(a && a.delta) || 0} 后可交付量`
}

// 结论文案：把数字翻译成业务语言（这才是“分析”的产出）
const conclusion = computed(() => {
  const a = analysis.value
  if (!a || !a.summary) return { title: '', lines: [] }
  const s = a.summary
  const lines = []

  if (a.mode === 'expand') {
    lines.push(
      s.affected.length
        ? `扩容 +${a.delta} 后共有 ${s.affected.length} 条线路能提升可交付量：${labelList(s.affected)}。`
        : `扩容 +${a.delta} 对任何一条线路都无效，说明限制不在单条线路的运力上。`,
    )
    if (s.affected.length) {
      lines.push('其余线路加大容量后最大流不变——不在最小割里的边，扩容属于无效投入。')
    }
  } else {
    if (s.worst) {
      lines.push(
        `风险最高的是「${s.worst.label}」：${scenarioPhrase(a.mode, a)} ${a.base} → ${s.worst.after}，` +
          `影响 ${deltaText(s.worst)}。${
            s.worst.isCut
              ? '它同时是瓶颈线路，属于既要扩容又要防故障的重点对象。'
              : '它不在瓶颈线路里，说明这里存在单点依赖，建议增加备用通道。'
          }`,
      )
    } else {
      lines.push('任何一条线路出问题都不影响可交付量，当前网络没有单点风险。')
    }
    lines.push(
      `共 ${s.fatalCount} 条致命、${s.sensitiveCount} 条敏感、${s.redundantCount} 条冗余线路` +
        `（判定口径：影响率 ≥ 40% 致命、10% ~ 40% 敏感）。`,
    )
  }

  return {
    title:
      `结论 · 当前最大可交付 ${a.base} ${a.unit}` +
      (a.gap > 0 ? `，缺口 ${a.gap} ${a.unit}` : ''),
    lines,
  }
})

watch(
  () => (summary.value ? summary.value.cutEdges.map((e) => e.key).join(',') : ''),
  () => {
    const edges = summary.value ? summary.value.cutEdges : []
    if (!edges.some((e) => e.key === selectedCutKey.value)) {
      selectedCutKey.value = edges[0] ? edges[0].key : ''
    }
    simResult.value = null
  },
)

async function loadPreset() {
  const ok = await loadGraphText(SUPPLY_CHAIN_SAMPLE.text, SUPPLY_CHAIN_SAMPLE.name)
  if (!ok) return
  setSupplyChainMeta(SUPPLY_CHAIN_SAMPLE)
  store.source = SUPPLY_CHAIN_SAMPLE.source
  store.sink = SUPPLY_CHAIN_SAMPLE.sink
  await nextTick()
  if (store.flowControls) store.flowControls.compute()
  notify('已启用供应链场景，并自动计算最大可交付量和瓶颈线路', 'success')
}

function clearPreset() {
  setSupplyChainMeta(null)
  simResult.value = null
  analysis.value = null
  analysisCache.clear()
  store.focusEdge = null
  notify('已退出供应链场景，图本身保持不变', 'info')
}

async function simulate() {
  const core = coreRef()
  const edge = selectedCut.value
  if (!core || !store.graph || !edge) return
  const add = Math.max(1, Math.floor(delta.value || 1))
  const original = await exportGraphText()
  if (original == null) return
  const modified = buildGraphTextWithExtraCapacity(store.graph, edge.u, edge.v, add)
  simulating.value = true
  try {
    const loaded = core.loadText(modified, 'what-if')
    if (!loaded.ok) {
      notify(loaded.error, 'error')
      return
    }
    const res = core.maxFlow(store.source, store.sink)
    if (!res.ok) {
      notify(res.error, 'error')
      return
    }
    simResult.value = {
      edge,
      delta: add,
      from: summary.value ? summary.value.maxFlow : res.value,
      to: res.value,
      gain: summary.value ? res.value - summary.value.maxFlow : 0,
    }
  } finally {
    core.loadText(original, store.fileName || 'restore')
    simulating.value = false
  }
}

// ---------------- 问题定位：线路影响分析 ----------------

// 切情景就重算（同一情景 + 同一张图命中缓存，不重复跑）
function setMode(id) {
  analysisMode.value = id
  runAnalysis()
}

// 把输入框里的数字收进合法范围（用户可能手填 0 或者 1000）
function normalizedParams() {
  const delta = Math.min(
    EXPAND_RANGE.max,
    Math.max(EXPAND_RANGE.min, Math.floor(Number(analysisDelta.value) || EXPAND_RANGE.default)),
  )
  const percent = Math.min(
    DEGRADE_RANGE.max,
    Math.max(DEGRADE_RANGE.min, Math.floor(Number(degradePercent.value) || DEGRADE_RANGE.default)),
  )
  return { delta, percent }
}

// 在当前情景下改了参数才重算（改另一个情景的参数只记下来，等切过去再用）
function onParamChange(mode) {
  if (analysisMode.value !== mode) return
  runAnalysis()
}

// 点表格里的一行：在画布上高亮这条线路，再点一次取消
function focusLine(row) {
  store.focusEdge = store.focusEdge === row.key ? null : row.key
}

async function runAnalysis(force = false) {
  const core = coreRef()
  const meta = store.supplyChainMeta
  if (!core || !active.value || !store.graph || !store.graph.edges.length) return

  const { delta, percent } = normalizedParams()
  analysisDelta.value = delta
  degradePercent.value = percent

  const cacheKey = [
    analysisMode.value,
    delta,
    percent,
    store.source,
    store.sink,
    store.graph.n,
    store.graph.edges.length,
  ].join('|')
  if (!force && analysisCache.has(cacheKey)) {
    analysis.value = analysisCache.get(cacheKey)
    return
  }

  // 分析要在“影子图”上做，所以先把当前图文本存下来给它还原用
  const original = await exportGraphText()
  if (original == null) return

  analyzing.value = true
  try {
    const res = analyzeLineImpact(core, store.graph, meta, {
      originalText: original,
      mode: analysisMode.value,
      delta,
      ratio: percent / 100,
      source: store.source,
      sink: store.sink,
    })
    if (!res.ok) {
      analysis.value = null
      notify(res.error, 'error')
      return
    }
    analysisCache.set(cacheKey, res)
    analysis.value = res
    expanded.value = false
    store.focusEdge = null
    notify(
      `线路影响分析完成：共分析 ${res.rows.length} 条线路，其中 ${res.summary.affected.length} 条有影响`,
      'success',
    )
  } finally {
    analyzing.value = false
  }
}

// 进入场景后自动跑一次；退出场景 / 换图 / 改源汇点就把结果清掉（避免展示过期数据）
watch(active, async (on) => {
  if (!on) {
    analysis.value = null
    analysisCache.clear()
    store.focusEdge = null
    return
  }
  await nextTick()
  runAnalysis()
})

watch(
  () => [store.source, store.sink, store.graph],
  () => {
    analysis.value = null
    store.focusEdge = null
  },
)
</script>

<style scoped>
.supply-panel .panel-body {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.role-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 10px;
  padding: 7px 8px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--line-soft);
  background: #0e1727;
}
.mini-title {
  margin-top: 4px;
  color: var(--text-dim);
  font-size: 12px;
  letter-spacing: 0.3px;
}
.biz-card {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 9px 10px;
  border-radius: var(--radius-sm);
  border: 1px solid #27547a;
  background: rgba(56, 189, 248, 0.09);
}
.biz-card b {
  color: #dff6ff;
  font-size: 13px;
}
.biz-card p {
  color: var(--text-dim);
  font-size: 12px;
  line-height: 1.55;
}
.cut-list,
.util-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.cut-row,
.util-row {
  display: grid;
  align-items: center;
  gap: 7px;
  padding: 5px 7px;
  border-radius: 6px;
  border: 1px solid var(--line-soft);
  background: #101a2b;
  color: var(--text-dim);
  font-size: 12px;
}
.cut-row {
  grid-template-columns: 1fr auto;
}
.cut-row b {
  color: #f0abfc;
}
.util-row {
  grid-template-columns: minmax(0, 1fr) 74px auto;
}
.u-name {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.u-bar {
  height: 5px;
  border-radius: 999px;
  background: #16233a;
  overflow: hidden;
}
.u-bar i {
  display: block;
  height: 100%;
  border-radius: 999px;
  background: linear-gradient(90deg, #22d3ee, #34d399);
}
.util-row b {
  color: #cfe6ff;
  font-size: 11px;
}
select,
.delta {
  min-width: 0;
  height: 28px;
  border-radius: 7px;
  border: 1px solid var(--line);
  background: #0e1727;
  color: var(--text);
  padding: 0 7px;
}
select {
  flex: 1;
}
.delta {
  width: 64px;
}

/* ---------- 问题定位：线路影响分析 ---------- */
.analysis-title {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
  flex-wrap: wrap;
}
.impact-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
  max-height: 240px;
  overflow: auto;
  /* 别被 flex 压扁：overflow 不是 visible 时最小高度会塌成 0，整张表会看不见 */
  flex-shrink: 0;
}
.impact-row {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  grid-template-areas:
    'name tag'
    'meta delta';
  gap: 1px 8px;
  padding: 5px 8px;
  border-radius: 7px;
  border: 1px solid var(--line);
  background: #101a2b;
  color: var(--text-dim);
  font-size: 12px;
  text-align: left;
  cursor: pointer;
}
.impact-row:hover {
  border-color: #3a527d;
}
.impact-row.on {
  border-color: var(--cyan);
  background: rgba(34, 211, 238, 0.1);
}
.i-name {
  grid-area: name;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: #cfe6ff;
}
.i-tag {
  grid-area: tag;
  font-size: 11px;
  white-space: nowrap;
}
.i-meta {
  grid-area: meta;
  font-size: 11px;
  color: var(--text-faint);
}
.i-delta {
  grid-area: delta;
  font-size: 11.5px;
  font-weight: 600;
  text-align: right;
}
.impact-row.fatal .i-tag,
.impact-row.fatal .i-delta {
  color: var(--red);
}
.impact-row.sensitive .i-tag,
.impact-row.sensitive .i-delta {
  color: var(--orange);
}
.impact-row.minor .i-tag,
.impact-row.minor .i-delta {
  color: var(--amber);
}
.impact-row.none .i-tag {
  color: var(--text-faint);
}
.impact-row.gain .i-tag,
.impact-row.gain .i-delta {
  color: var(--green);
}
.impact-row.gainNone .i-tag {
  color: var(--text-faint);
}
</style>
