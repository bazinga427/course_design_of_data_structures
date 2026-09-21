<template>
  <div class="panel">
    <div class="panel-head">
      <h3>遍历 · DFS / BFS</h3>
      <span class="hint">{{ mode === 'dfs' ? '深度优先（栈 / 递归）' : '广度优先（队列）' }}</span>
    </div>

    <div class="panel-body">
      <div class="row">
        <div class="field">
          <span>方式</span>
          <button class="btn tiny" :class="{ active: mode === 'dfs' }" @click="mode = 'dfs'">
            DFS
          </button>
          <button class="btn tiny" :class="{ active: mode === 'bfs' }" @click="mode = 'bfs'">
            BFS
          </button>
        </div>
        <div class="field">
          <span>起点</span>
          <select v-model.number="start" :disabled="!graph">
            <option v-for="id in nodeIds" :key="id" :value="id">{{ id }}</option>
          </select>
        </div>
        <button class="btn tiny" :disabled="!graph" @click="useSelectedNode">用选中点</button>
      </div>

      <div class="row">
        <button class="btn primary" :disabled="!graph" @click="compute">计算遍历序列</button>
        <button class="btn" :disabled="!result || playing" @click="play">播放</button>
        <button class="btn" :disabled="!result || !playing" @click="pause">暂停</button>
        <button class="btn" :disabled="!result" @click="step">单步</button>
        <button class="btn" :disabled="!result" @click="reset">重置</button>
      </div>

      <div class="row" v-if="result">
        <span class="field"><span>速度</span>
          <input type="range" min="120" max="1400" step="20" v-model.number="speed" />
          <span class="hint">{{ speed }} ms/步</span>
        </span>
        <span class="hint grow">进度 {{ Math.max(index + 1, 0) }} / {{ seq.length }}</span>
      </div>

      <div v-if="result" class="seq-list">
        <button
          v-for="(id, i) in seq"
          :key="`${id}-${i}`"
          class="seq-item"
          :class="{ done: i < index, current: i === index }"
          @click="gotoStep(i)"
        >
          {{ id }}
        </button>
      </div>

      <p v-if="result && result.reachable.length < graph.n" class="hint warn">
        本次只覆盖连通块 { {{ result.reachable.join(', ') }} }，共 {{ result.reachable.length }} 个点；
        其余 {{ graph.n - result.reachable.length }} 个点从这个起点不可达（算法只走连通的点）。
      </p>
      <p v-else-if="result" class="hint ok">
        这个起点能走到全部 {{ graph.n }} 个点。
      </p>
    </div>

    <!-- ---------------- 遍历序列合法性判别 ---------------- -->
    <div class="sub-panel">
      <div class="sub-head">
        <h4>遍历序列合法性判别</h4>
        <span class="hint">判的不是“和标准答案一样”，而是“能不能真的走出来”</span>
      </div>
      <div class="panel-body">
        <textarea
          v-model="seqInput"
          rows="2"
          placeholder="输入一串点编号，用空格分开，例如：1 2 3 4 5 6"
          :disabled="!graph"
        ></textarea>
        <div class="row">
          <button class="btn primary" :disabled="!graph" @click="runCheck">判别</button>
          <button class="btn tiny" :disabled="!graph" @click="fillSample('dfs')">填入一条合法 DFS</button>
          <button class="btn tiny" :disabled="!graph" @click="fillSample('bfs')">填入一条合法 BFS</button>
          <button class="btn tiny" :disabled="!graph" @click="fillFromCurrent">填入刚才算出来的序列</button>
        </div>

        <template v-if="checkResult">
          <div class="row">
            <span class="chip" :class="checkResult.dfs ? 'ok' : 'bad'">
              合法 DFS {{ checkResult.dfs ? '✓' : '✗' }}
            </span>
            <span class="chip" :class="checkResult.bfs ? 'ok' : 'bad'">
              合法 BFS {{ checkResult.bfs ? '✓' : '✗' }}
            </span>
            <span class="chip">
              {{ checkResult.dfs && checkResult.bfs
                ? '两种都合法'
                : checkResult.dfs
                  ? '只作为 DFS 合法'
                  : checkResult.bfs
                    ? '只作为 BFS 合法'
                    : '两种都不是' }}
            </span>
          </div>
          <p class="hint mono">
            参考：一条合法 DFS = {{ checkResult.sampleDfs.join(' ') || '（空）' }}<br />
            参考：一条合法 BFS = {{ checkResult.sampleBfs.join(' ') || '（空）' }}
          </p>
          <p class="hint">
            注意合法答案不止一个：邻居的访问顺序换一下，序列就跟着变了。
          </p>
        </template>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import { store, coreRef, notify, setTraversalLayer, emptyLayer, edgeKey } from '../store/graphStore.js'

const mode = ref('dfs')
const start = ref(1)
const result = ref(null)
const index = ref(-1)
const playing = ref(false)
const speed = ref(620)
const seqInput = ref('')
const checkResult = ref(null)

let timer = null

const graph = computed(() => store.graph)
const nodeIds = computed(() => (graph.value ? graph.value.nodes : []))
const seq = computed(() => (result.value ? result.value.seq : []))

// ---------------- 动画：把当前进度写进画布高亮图层 ----------------
function applyLayer() {
  const layer = emptyLayer('traversal')
  if (!result.value || !graph.value) {
    setTraversalLayer(layer)
    return
  }

  const { seq: order, parentEdges } = result.value
  const position = new Map()
  order.forEach((id, i) => position.set(id, i))

  order.forEach((id, i) => {
    if (i < index.value) layer.nodes[id] = 'visited'
    else if (i === index.value) layer.nodes[id] = 'current'
  })
  if (index.value < 0 && order.length) layer.nodes[order[0]] = 'frontier'

  // 树边：子节点已经走过之后，把父 -> 子这条边走线高亮出来
  for (const [p, c] of parentEdges) {
    const pos = position.get(c)
    if (pos !== undefined && pos <= index.value) layer.edges[edgeKey(p, c)] = 'tree'
  }

  layer.title = `${mode.value.toUpperCase()} 从 ${result.value.start} 出发`
  setTraversalLayer(layer)
}

function stopTimer() {
  if (timer) {
    clearInterval(timer)
    timer = null
  }
  playing.value = false
}

function play() {
  if (!result.value) return
  if (index.value >= seq.value.length - 1) index.value = -1
  stopTimer()
  playing.value = true
  timer = setInterval(() => {
    if (index.value >= seq.value.length - 1) {
      stopTimer()
      return
    }
    index.value += 1
  }, speed.value)
}

function pause() {
  stopTimer()
}

function step() {
  stopTimer()
  if (!result.value) return
  index.value = Math.min(index.value + 1, seq.value.length - 1)
}

function reset() {
  stopTimer()
  index.value = -1
}

function gotoStep(i) {
  stopTimer()
  index.value = i
}

function compute() {
  const core = coreRef()
  if (!core || !graph.value) return
  const res = core.traverse(mode.value === 'bfs' ? 1 : 0, start.value)
  if (!res.ok) {
    notify(res.error, 'error')
    return
  }
  stopTimer()
  result.value = res
  index.value = 0
  notify(
    `${mode.value.toUpperCase()} 从 ${start.value} 出发：${res.seq.join(' ')}`,
    'success',
  )
}

function useSelectedNode() {
  if (store.selectedNode != null) start.value = store.selectedNode
  else notify('先在画布上点一个节点', 'warn')
}

// ---------------- 序列合法性判别 ----------------
function runCheck() {
  const core = coreRef()
  if (!core || !graph.value) return
  const res = core.checkSequence(start.value, seqInput.value)
  if (!res.ok) {
    checkResult.value = null
    notify(res.error, 'error')
    return
  }
  checkResult.value = res
}

async function fillSample(kind) {
  const core = coreRef()
  if (!core) return
  const res = core.traverse(kind === 'bfs' ? 1 : 0, start.value)
  if (res.ok) {
    seqInput.value = res.seq.join(' ')
    checkResult.value = null
  }
}

function fillFromCurrent() {
  if (!result.value) {
    notify('先点「计算遍历序列」', 'warn')
    return
  }
  seqInput.value = result.value.seq.join(' ')
  checkResult.value = null
}

watch([result, index], applyLayer)
watch(speed, () => {
  if (playing.value) play()
})
watch(
  () => store.graph,
  () => {
    stopTimer()
    result.value = null
    index.value = -1
    checkResult.value = null
    seqInput.value = ''
    start.value = graph.value && graph.value.n >= 1 ? 1 : 1
    applyLayer()
  },
)

onBeforeUnmount(() => {
  stopTimer()
  setTraversalLayer(emptyLayer())
})
</script>

<style scoped>
.sub-panel {
  border-top: 1px solid var(--line-soft);
  background: #0d1626;
}
.sub-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
  padding: 10px 12px 0;
  flex-wrap: wrap;
}
.sub-head h4 {
  font-size: 12.5px;
  color: var(--text-dim);
  font-weight: 600;
  letter-spacing: 0.3px;
}
.chip.ok {
  color: #b7f7e6;
  border-color: #1f6f5c;
  background: #0d2b26;
}
.chip.bad {
  color: #ffd9d9;
  border-color: #7f3b3b;
  background: #2b1414;
}
</style>
