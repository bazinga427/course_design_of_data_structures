<template>
  <aside class="panel narrator-col" :class="{ collapsed: !open }">
    <div class="panel-head">
      <h3>过程解说</h3>
      <div class="row tight">
        <span class="hint" v-if="total">{{ index + 1 }} / {{ total }}</span>
        <button class="btn tiny" @click="open = !open">{{ open ? '收起' : '展开' }}</button>
      </div>
    </div>

    <div v-if="open" class="narrator-body">
      <template v-if="frame">
        <div class="progress"><i :class="frame.phase" :style="{ width: percent + '%' }"></i></div>

        <div class="head-line" :class="frame.phase">{{ view.head }}</div>

        <div class="kv" v-for="(r, i) in view.rows" :key="i">
          <span class="k">{{ r.k }}</span>
          <span class="v mono">{{ r.v }}</span>
        </div>

        <div class="timeline" ref="timelineEl">
          <template v-for="g in groups" :key="g.key">
            <div class="tl-group">{{ g.title }}</div>
            <button
              v-for="f in g.items"
              :key="f.id"
              class="tl-item"
              :class="[f.phase, { current: f.id === index }]"
              :data-frame="f.id"
              @click="jump(f.id)"
            >
              <i class="tl-dot" :class="f.phase"></i>
              <span class="tl-text">{{ shortLabel(f) }}</span>
            </button>
          </template>
        </div>
      </template>

      <div v-else class="empty">
        <p>点左边「计算最大流 / 最小割」，这里会按每一条增广路逐步显示。</p>
      </div>
    </div>
  </aside>
</template>

<script setup>
import { computed, nextTick, ref, watch } from 'vue'
import { store } from '../store/graphStore.js'

const open = ref(true)
const timelineEl = ref(null)

const frames = computed(() => store.flowFrames || [])
const index = computed(() => store.flowIndex)
const total = computed(() => store.flowTotal)
const frame = computed(() => store.flowFrame)
const percent = computed(() =>
  total.value > 0 ? Math.round(((index.value + 1) / total.value) * 100) : 0,
)

// 解说栏只回答四件事：第几条路、什么路线、当前哪个点到哪个点、余额减多少。
// 最小割分三步播：① 整理残余网络（擦掉余额 0 的弧）② 分点集（从 s 往外扩散）
// ③ 割（剩下的点归 T，S→T 的弧就是割边），每一步都把关键数字列出来，方便核对容量和。
const view = computed(() => {
  const f = frame.value
  if (!f) return { head: '', rows: [] }
  return graphFrameView(f)
})

function graphFrameView(f) {
  const rows = []
  let head = ''
  if (f.kind === 'init') head = '初始化'
  else if (f.kind === 'noPath') head = '结束判定 · 没有增广路了'
  else if (f.kind === 'prune') head = '最小割 ① · 整理残余网络'
  else if (f.kind === 'grow') head = `最小割 ② · 分点集（第 ${f.growInfo ? f.growInfo.level : 1} 层）`
  else if (f.kind === 'cut') head = `最小割 ③ · 割边容量和 = ${f.totals.cutCapacity}`
  else head = `第 ${f.round} 条增广路`

  if (f.path && f.path.length > 1) rows.push({ k: '路线', v: f.path.join(' → ') })
  if (f.focusArc) rows.push({ k: '当前', v: `${f.focusArc[0]} → ${f.focusArc[1]}` })
  if (f.kind === 'route') rows.push({ k: '余额', v: `本条路瓶颈 ${f.bottleneck}` })
  if (f.kind === 'push' && f.balance) {
    const b = f.balance
    rows.push({ k: '余额', v: `${b.before} → ${b.after}（减 ${b.delta}）` })
  }
  if (f.kind === 'prune' && f.droppedEdges) {
    rows.push({
      k: '擦掉',
      v: `${f.droppedEdges.length} 条余额 = 0 的弧（推满的正向弧、撤完的反向弧）`,
    })
  }
  if (f.kind === 'grow' && f.growInfo) {
    const added = f.growInfo.added
    rows.push({
      k: '新走到',
      v: added.length ? `{ ${added.join(', ')} }` : '没有：从源点一条余额 > 0 的弧都走不出去',
    })
    rows.push({ k: '已归 S', v: `{ ${f.growInfo.seen.join(', ')} }` })
  }
  if (f.sets) rows.push({ k: 'S / T', v: `{ ${f.sets.S.join(', ')} }  /  { ${f.sets.T.join(', ')} }` })
  if (f.kind === 'cut') rows.push({ k: '割边', v: f.cutSummary })
  return { head, rows, tone: f.phase }
}

const shortLabel = (f) => {
  switch (f.kind) {
    case 'init':
      return '初始化'
    case 'route':
      return `路线 ${f.path.join('→')}`
    case 'push':
      if (!f.balance) return '推流'
      return `${f.balance.u}→${f.balance.v} 余额 ${f.balance.before}→${f.balance.after}`
    case 'noPath':
      return '结束判定'
    case 'prune':
      return '① 整理残余网络'
    case 'grow':
      return `② ${f.label}`
    case 'cut':
      return `③ 割 = ${f.cutCapacity}`
    default:
      return f.label
  }
}

// 时间线分组：准备 / 第 k 条增广路 / 结束与最小割
const groups = computed(() => {
  const out = []
  for (const f of frames.value) {
    const title =
      f.kind === 'init'
        ? '准备'
        : f.roundIndex >= f.rounds
          ? '结束与最小割'
          : `第 ${f.round} 条增广路`
    const last = out[out.length - 1]
    if (last && last.title === title) last.items.push(f)
    else out.push({ key: `${f.id}-${title}`, title, items: [f] })
  }
  return out
})

function jump(i) {
  const controls = store.flowControls
  if (controls) controls.jump(i)
}

// 当前帧滚到看得见的位置（只在时间线内部滚动，不动整个页面）
watch(index, async () => {
  await nextTick()
  const host = timelineEl.value
  if (!host) return
  const el = host.querySelector(`[data-frame="${index.value}"]`)
  if (!el) return
  const top = el.offsetTop
  const bottom = top + el.offsetHeight
  if (top < host.scrollTop || bottom > host.scrollTop + host.clientHeight) {
    host.scrollTop = Math.max(0, top - host.clientHeight / 2)
  }
})
</script>

<style scoped>
.narrator-col {
  width: 318px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}
.narrator-col.collapsed {
  width: 148px;
}

.narrator-body {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 12px;
  overflow: auto;
}

.progress {
  height: 3px;
  border-radius: 999px;
  background: #16233a;
  overflow: hidden;
  flex-shrink: 0;
}
.progress i {
  display: block;
  height: 100%;
  border-radius: 999px;
  background: linear-gradient(90deg, #fbbf24, #f59e0b);
  transition: width 0.2s ease;
}
.progress i.augment {
  background: linear-gradient(90deg, #34d399, #22d3ee);
}
.progress i.stop {
  background: linear-gradient(90deg, #fb923c, #f87171);
}
.progress i.prune {
  background: linear-gradient(90deg, #64748b, #94a3b8);
}
.progress i.grow {
  background: linear-gradient(90deg, #3b82f6, #60a5fa);
}
.progress i.cut {
  background: linear-gradient(90deg, #a78bfa, #f472b6);
}

.head-line {
  font-size: 15px;
  font-weight: 650;
  letter-spacing: 0.3px;
  padding-left: 9px;
  border-left: 3px solid var(--amber);
}
.head-line.augment {
  border-left-color: var(--green);
}
.head-line.stop {
  border-left-color: var(--orange);
}
.head-line.prune {
  border-left-color: #94a3b8;
}
.head-line.grow {
  border-left-color: #60a5fa;
}
.head-line.cut {
  border-left-color: var(--violet);
}
.head-line.init {
  border-left-color: var(--cyan);
}

.kv {
  display: flex;
  align-items: baseline;
  gap: 8px;
  padding: 6px 9px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--line-soft);
  background: #0e1727;
}
.kv .k {
  flex-shrink: 0;
  font-size: 12px;
  color: var(--text-faint);
}
.kv .v {
  font-size: 13.5px;
  color: #e8f2ff;
  word-break: break-all;
}

.bal {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 7px 9px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--line-soft);
  background: #0e1727;
  font-size: 12.5px;
}
.bal-row {
  display: grid;
  grid-template-columns: 1fr auto auto auto;
  gap: 8px;
  color: var(--text-dim);
}
.bal-row .edge {
  color: #cfe6ff;
}
.bal-row .before {
  color: var(--text-dim);
}
.bal-row .arrow {
  color: var(--text-faint);
}
.bal-row .after {
  color: var(--green);
  min-width: 18px;
  text-align: right;
}

.timeline {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 2px;
  max-height: 420px;
  overflow: auto;
  padding-right: 2px;
  border-top: 1px solid var(--line-soft);
  padding-top: 8px;
}
.tl-group {
  position: sticky;
  top: 0;
  z-index: 2;
  padding: 4px 6px;
  margin-top: 4px;
  font-size: 11.5px;
  color: var(--text-dim);
  background: linear-gradient(180deg, #16233a, #111c2e);
  border-radius: 6px;
}
.tl-item {
  display: flex;
  align-items: flex-start;
  gap: 7px;
  padding: 4px 8px;
  border-radius: 6px;
  border: 1px solid transparent;
  background: transparent;
  color: var(--text-dim);
  cursor: pointer;
  font-size: 12px;
  text-align: left;
  line-height: 1.45;
}
.tl-item:hover {
  background: #16233a;
  color: var(--text);
}
.tl-item.current {
  border-color: #2f7f99;
  background: rgba(34, 211, 238, 0.12);
  color: #dff8ff;
}
.tl-dot {
  flex-shrink: 0;
  width: 6px;
  height: 6px;
  margin-top: 6px;
  border-radius: 50%;
  background: #3d5175;
}
.tl-dot.search {
  background: var(--amber);
}
.tl-dot.augment {
  background: var(--green);
}
.tl-dot.stop {
  background: var(--orange);
}
.tl-dot.prune {
  background: #94a3b8;
}
.tl-dot.grow {
  background: #60a5fa;
}
.tl-dot.cut {
  background: var(--violet);
}

.empty {
  color: var(--text-dim);
  font-size: 12.5px;
  line-height: 1.7;
  padding: 4px;
}

.mono {
  font-family: 'Cascadia Mono', 'Consolas', ui-monospace, monospace;
}
</style>
