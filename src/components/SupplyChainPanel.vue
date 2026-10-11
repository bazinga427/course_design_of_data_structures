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
  buildGraphTextWithExtraCapacity,
  buildSupplyChainSummary,
  supplyChainSentence,
} from '../domain/supplyChain.js'

const delta = ref(5)
const selectedCutKey = ref('')
const simulating = ref(false)
const simResult = ref(null)

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
</style>