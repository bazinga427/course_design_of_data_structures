<template>
  <div class="panel">
    <div class="panel-head">
      <h3>最大流 · 最小割</h3>
      <span class="hint">Ford-Fulkerson（DFS 找增广路）</span>
    </div>

    <div class="panel-body">
      <div class="row">
        <div class="field">
          <span>源点 s</span>
          <select v-model.number="sourceValue" :disabled="!graph">
            <option v-for="id in nodeIds" :key="`s${id}`" :value="id">{{ id }}</option>
          </select>
        </div>
        <div class="field">
          <span>汇点 t</span>
          <select v-model.number="sinkValue" :disabled="!graph">
            <option v-for="id in nodeIds" :key="`t${id}`" :value="id">{{ id }}</option>
          </select>
        </div>
        <button class="btn tiny" :disabled="!graph" @click="useSelected('s')">选中点作 s</button>
        <button class="btn tiny" :disabled="!graph" @click="useSelected('t')">选中点作 t</button>
      </div>

      <div class="row">
        <button class="btn accent" :disabled="!graph" @click="compute">计算最大流 / 最小割</button>
        <button class="btn" :disabled="!total || playing" @click="play">播放</button>
        <button class="btn" :disabled="!total || !playing" @click="pause">暂停</button>
        <button class="btn" :disabled="!total" @click="step">单步</button>
        <button class="btn" :disabled="!total" @click="reset">重置</button>
      </div>

      <div class="row" v-if="result">
        <span class="field"><span>速度</span>
          <input type="range" min="200" max="2000" step="50" v-model.number="speed" />
          <span class="hint">{{ speed }} ms/步</span>
        </span>
        <div class="field">
          <span>边上显示</span>
          <button class="btn tiny" :class="{ active: view === 'res' }" @click="view = 'res'">
            余额 / 总容量
          </button>
          <button class="btn tiny" :class="{ active: view === 'flow' }" @click="view = 'flow'">
            流量 / 总容量
          </button>
        </div>
      </div>

      <template v-if="result">
        <div class="stat-line">
          <div class="stat green"><span>最大流</span><b>{{ result.value }}</b></div>
          <div class="stat violet"><span>最小割容量</span><b>{{ result.cutCapacity }}</b></div>
          <div class="stat cyan">
            <span>增广次数</span><b>{{ result.steps.length }}</b>
          </div>
        </div>

        <p class="hint" :class="result.correct ? 'ok' : 'bad'">
          {{ result.correct
            ? '最大流 = 最小割 ✓（割边容量之和正好等于最大流，算法结果自洽）'
            : '最大流和最小割对不上，算法有问题！' }}
        </p>

        <div class="row">
          <span class="chip">S = { {{ result.sourceSide.join(', ') }} }</span>
          <span class="chip">T = { {{ result.sinkSide.join(', ') }} }</span>
        </div>

        <p class="hint">
          割边：
          <span class="mono">
            {{ result.cutEdges.length
              ? result.cutEdges.map(([u, v]) => `${u}→${v}(容量 ${capOf(u, v)})`).join('，')
              : '（没有割边，说明 s 到 t 本来就不通，最大流为 0）' }}
          </span>
        </p>

        <div class="steps">
          <button
            v-for="(s, i) in result.steps"
            :key="i"
            class="step-item"
            :class="{ done: i < currentRoundIndex, current: i === currentRoundIndex }"
            @click="jumpToRound(i)"
          >
            <span class="mono">{{ s.path.join('→') }}</span>
            <b>+{{ s.bottleneck }}</b>
            <span class="hint">总流量 {{ s.flowAfter }}</span>
          </button>
          <button
            class="step-item finish"
            :class="{ current: finished }"
            @click="jumpToRound(result.steps.length)"
          >
            <span>完成 · 显示最小割</span>
          </button>
        </div>

        <p class="hint">
          边上写的是「余 x / 总 y」：x 是余额（这条弧还能再推多少），y 是权重（这条弧的容量）。
          切到「流量 / 总容量」就变成「流 x / 总 y」。
          红色 = 本条增广路，绿色加粗 = 正在推流的这条弧，粉色虚线 = 割边，
          蓝色 S 侧（源点）/ 紫色 T 侧（汇点）。
          青蓝虚线 = 反向弧：原图里没有这条弧，它的余额就是已经推过去的流里能撤回多少。
        </p>
      </template>
    </div>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref, shallowRef, watch } from 'vue'
import {
  store,
  coreRef,
  notify,
  setFlowLayer,
  emptyLayer,
  edgeKey,
  reverseEdgeKey,
  resetFlowNarration,
} from '../store/graphStore.js'
import { buildNarration, makeCapLookup, arcKey } from '../flow/narrator.js'

const result = ref(null)
const frames = shallowRef([])
const index = ref(-1)
const playing = ref(false)
const speed = ref(900)
const view = ref('res')
const verified = ref(true)

let timer = null

const graph = computed(() => store.graph)
const nodeIds = computed(() => (graph.value ? graph.value.nodes : []))
const capLookup = computed(() => makeCapLookup(graph.value))

const total = computed(() => frames.value.length)
const current = computed(() =>
  index.value >= 0 && index.value < frames.value.length ? frames.value[index.value] : null,
)
const currentRoundIndex = computed(() => (current.value ? current.value.roundIndex : -1))
const finished = computed(() => total.value > 0 && index.value >= total.value - 1)

// 直接在面板上改 store 里的源点 / 汇点，画布上的蓝圈紫圈会跟着动
const sourceValue = computed({
  get: () => store.source,
  set: (v) => {
    store.source = v
  },
})
const sinkValue = computed({
  get: () => store.sink,
  set: (v) => {
    store.sink = v
  },
})

function capOf(u, v) {
  return capLookup.value(u, v)
}

// ------------------------------------------------------------------
//  每一帧怎么画：帧里带了“这一步该高亮哪些点 / 哪些弧”，
//  边的数字则统一按“当前残余网络”算，所以面板上的解说和画布永远一致。
// ------------------------------------------------------------------
function applyLayer() {
  const layer = emptyLayer('flow')
  const g = graph.value
  if (!g) {
    setFlowLayer(layer)
    return
  }

  const f = current.value
  const flows = (f && f.flows) || {}
  const netOf = (u, v) => flows[arcKey(u, v)] || 0

  if (f) {
    Object.assign(layer.nodes, f.focusNodes || {})
    Object.assign(layer.edges, f.focusEdges || {})
  }

  // 已经推过流、又没有被本帧特别标出来的弧，统一画成绿色
  for (const e of g.edges) {
    const k = edgeKey(e.u, e.v)
    if (!layer.edges[k] && netOf(e.u, e.v) > 0) layer.edges[k] = 'flowEdge'
  }

  // 弧上的字：这一帧专门给的标注优先，其次是“流量/容量”或“余 / 退”
  for (const e of g.edges) {
    const k = edgeKey(e.u, e.v)
    const net = netOf(e.u, e.v)
    const flowNow = Math.max(0, net)
    layer.edgeLabels[k] =
      view.value === 'flow'
        ? `流${flowNow}/总${e.cap}`
        : `余${e.cap - net}/总${e.cap}`
  }

  // 反向弧：只要正方向推过流（净流量 > 0），残余网络里反向的那条弧就有余额，
  // 这些余额就是“可以撤回来的流量”，同样标成「余 x / 总 y」画到图上。
  if (g.edges.length <= 400) {
    const listed = new Set(g.edges.map((e) => `${e.u}_${e.v}`))
    for (const e of g.edges) {
      if (listed.has(`${e.v}_${e.u}`)) continue // 反方向的弧本来就在图里
      const net = netOf(e.u, e.v)
      if (!net) continue
      const k = reverseEdgeKey(e.v, e.u)
      const backCap = capLookup.value(e.v, e.u)
      const backResidual = backCap - netOf(e.v, e.u)
      layer.edgeLabels[k] =
        view.value === 'flow'
          ? `流${net}/总${backCap}`
          : `余${backResidual}/总${backCap}`
      // 不改颜色：反向弧保持自己的青蓝虚线，只有本帧正在推它时
      // 才会被前面 focusEdges 里的高亮盖成绿色加粗
    }
  }

  layer.title =
    f && f.totals ? `总流量 ${f.totals.flow} / 最大流 ${f.totals.maxFlow}` : '最大流'
  setFlowLayer(layer)
}

// 把当前帧同步给右侧的解说栏
function publish() {
  store.flowIndex = index.value
  store.flowFrame = current.value
  store.flowTotal = total.value
  store.flowPlaying = playing.value
  store.flowVerified = verified.value
}

function stopTimer() {
  if (timer) {
    clearInterval(timer)
    timer = null
  }
  playing.value = false
}

function play() {
  if (!total.value) return
  if (index.value >= total.value - 1) index.value = -1
  stopTimer()
  playing.value = true
  publish()
  timer = setInterval(() => {
    if (index.value >= frames.value.length - 1) {
      stopTimer()
      publish()
      return
    }
    index.value += 1
  }, speed.value)
}

function pause() {
  stopTimer()
  publish()
}

function step() {
  stopTimer()
  if (!total.value) return
  index.value = Math.min(index.value + 1, frames.value.length - 1)
  publish()
}

function reset() {
  stopTimer()
  index.value = -1
  publish()
}

function jump(i) {
  stopTimer()
  if (!total.value) return
  index.value = Math.max(-1, Math.min(i, frames.value.length - 1))
  publish()
}

// 左侧轮次列表：跳到一个轮次的第一帧
function jumpToRound(roundIndex) {
  const k = frames.value.findIndex((f) => f.roundIndex === roundIndex)
  if (k >= 0) jump(k)
}

function compute() {
  const core = coreRef()
  if (!core || !graph.value) return
  if (graph.value.n < 2) {
    notify('至少要有两个点才能算最大流', 'warn')
    return
  }
  const res = core.maxFlow(store.source, store.sink)
  if (!res.ok) {
    notify(res.error, 'error')
    return
  }
  stopTimer()
  result.value = res
  store.flowResult = res

  // 不传 keyOf：让 narrator 自己算图层键，它会把“反向弧”用单独的键区分开
  const narration = buildNarration(res, graph.value)
  frames.value = narration.frames
  verified.value = narration.verified
  // 右侧解说栏的时间线只用这些轻量字段，不必把每一帧的残余数据都塞进响应式状态
  store.flowFrames = narration.frames.map((f) => ({
    id: f.id,
    kind: f.kind,
    phase: f.phase,
    round: f.round,
    roundIndex: f.roundIndex,
    group: f.group,
    label: f.label,
    path: f.path,
    focusArc: f.focusArc,
    focusResidual: f.focusResidual,
    retreat: f.retreat,
    bottleneck: f.bottleneck,
    balance: f.balance,
    rounds: f.totals ? f.totals.rounds : 0,
    cutCapacity: f.totals ? f.totals.cutCapacity : 0,
  }))
  index.value = -1
  applyLayer()
  publish()

  notify(
    `最大流 = ${res.value}，最小割容量 = ${res.cutCapacity}，共 ${res.steps.length} 次增广` +
      (narration.verified ? '' : '（过程重放和核心结果对不上，已切换成简化讲解）'),
    res.correct && narration.verified ? 'success' : res.correct ? 'warn' : 'error',
  )
  if (narration.frames.length) play()
}

function useSelected(kind) {
  if (store.selectedNode == null) {
    notify('先在画布上点一个节点', 'warn')
    return
  }
  if (kind === 's') store.source = store.selectedNode
  else store.sink = store.selectedNode
}

function clearResult() {
  stopTimer()
  result.value = null
  frames.value = []
  index.value = -1
  verified.value = true
  store.flowResult = null
  resetFlowNarration()
  applyLayer()
}

watch(index, () => {
  applyLayer()
  publish()
})
watch(view, applyLayer)

watch(
  () => [store.source, store.sink],
  () => {
    // 改了源点 / 汇点，之前的结果就不算数了
    if (result.value) clearResult()
  },
)
watch(speed, () => {
  if (playing.value) play()
})
watch(
  () => store.graph,
  () => {
    clearResult()
  },
)

onMounted(() => {
  // 右侧解说栏点时间线时，通过这些入口回到这里改播放状态
  store.flowControls = { compute, jump, play, pause, step, reset }
  publish()
})

onBeforeUnmount(() => {
  stopTimer()
  store.flowControls = null
  resetFlowNarration()
  setFlowLayer(emptyLayer())
})
</script>

<style scoped>
.steps {
  display: flex;
  flex-direction: column;
  gap: 5px;
  max-height: 168px;
  overflow: auto;
  padding-right: 2px;
}
.step-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 5px 9px;
  border-radius: 7px;
  border: 1px solid var(--line);
  background: #101a2b;
  color: var(--text-dim);
  cursor: pointer;
  font-size: 12px;
  text-align: left;
}
.step-item b {
  color: var(--green);
}
.step-item.done {
  border-color: #1f6b80;
  background: rgba(34, 211, 238, 0.1);
  color: #cdf6ff;
}
.step-item.current {
  border-color: var(--red);
  background: rgba(248, 113, 113, 0.12);
  color: #ffdcdc;
}
.step-item.finish {
  justify-content: center;
  border-color: #7c5bd1;
  color: #e5dcff;
}
.step-item.finish.current {
  background: rgba(167, 139, 250, 0.18);
}
</style>
