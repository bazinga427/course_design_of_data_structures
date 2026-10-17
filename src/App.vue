<template>
  <div class="app">
    <header class="app-header">
      <div class="brand">
        <span class="logo">图</span>
        <div>
          <h1>图结构综合实验平台</h1>
          <p>邻接表 / 邻接矩阵 · DFS / BFS 与序列判别 · 最大流最小割 · 物流供应链瓶颈分析 · 保存与加载</p>
        </div>
      </div>

      <div class="stats" v-if="graph">
        <span class="chip">顶点 <b>{{ graph.n }}</b></span>
        <span class="chip">边 <b>{{ graph.edges.length }}</b></span>
        <span class="chip">{{ graph.directed ? '有向图' : '无向图' }}</span>
        <span class="chip">编号 1..{{ graph.n }}</span>
        <span class="chip file">{{ fileName }}</span>
      </div>

      <div class="actions">
        <label class="btn primary">
          打开图文件
          <input type="file" accept=".txt,text/plain" hidden @change="onPickFile" />
        </label>

        <div class="menu-wrap">
          <button class="btn" @click="showSamples = !showSamples">载入示例 ▾</button>
          <div v-if="showSamples" class="menu">
            <button
              v-for="s in sampleGraphs"
              :key="s.id"
              class="menu-item"
              @click="loadSample(s)"
            >
              <b>{{ s.name }}</b>
              <span class="hint">{{ s.tip }}</span>
            </button>
          </div>
        </div>

        <button class="btn" @click="showRandom = true">随机生成</button>
        <button class="btn" @click="saveFile">保存图文件</button>
        <span class="badge" :class="backendClass"><i class="dot"></i>{{ backendLabel }}</span>
      </div>
    </header>

    <main class="app-body">
      <aside class="side">
        <div class="side-tabs" role="tablist" aria-label="功能分块">
          <button
            v-for="tab in sideTabs"
            :key="tab.id"
            class="side-tab"
            :class="{ active: activePanel === tab.id }"
            role="tab"
            :aria-selected="String(activePanel === tab.id)"
            @click="activePanel = tab.id"
          >
            <span class="tab-name">{{ tab.label }}</span>
            <span class="tab-sub">{{ tab.sub }}</span>
          </button>
        </div>

        <SupplyChainPanel v-show="activePanel === 'supply'" />
        <StoragePanel v-show="activePanel === 'storage'" />
        <TraversalPanel v-show="activePanel === 'traversal'" />
        <FlowAnimation v-show="activePanel === 'flow'" />
      </aside>
      <section class="stage">
        <GraphCanvas />
      </section>
      <FlowNarrator />
    </main>

    <!-- ---------------- 随机建图 ---------------- -->
    <div v-if="showRandom" class="modal-mask" @click.self="showRandom = false">
      <div class="modal">
        <div class="panel-head">
          <h3>随机生成一张图</h3>
          <span class="hint">同一个 seed 能复现同一张图</span>
        </div>
        <div class="modal-body">
          <div class="row">
            <label class="field grow"><span>顶点数 n</span>
              <input type="number" min="2" max="2000" v-model.number="form.n" />
            </label>
            <label class="field grow"><span>边数 m</span>
              <input type="number" min="0" v-model.number="form.m" />
            </label>
          </div>
          <div class="row">
            <label class="field grow"><span>类型</span>
              <select v-model.number="form.directed">
                <option :value="1">有向图</option>
                <option :value="0">无向图</option>
              </select>
            </label>
            <label class="field grow"><span>最大容量</span>
              <input type="number" min="1" v-model.number="form.maxCapacity" />
            </label>
          </div>
          <label class="field"><span>随机种子（0 = 每次都不一样）</span>
            <input type="number" min="0" v-model.number="form.seed" />
          </label>
          <p class="hint">
            提示：边数超过上限会自动收敛到上限；容量填 1 就是不带权的普通图，
            填 9 之类的小数字更适合演示最大流。
          </p>
        </div>
        <div class="modal-foot">
          <button class="btn" @click="showRandom = false">取消</button>
          <button class="btn primary" @click="doRandom">生成</button>
        </div>
      </div>
    </div>

    <div v-if="toast.text" class="toast" :class="toast.type">{{ toast.text }}</div>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref, watch } from 'vue'
import GraphCanvas from './components/GraphCanvas.vue'
import StoragePanel from './components/StoragePanel.vue'
import TraversalPanel from './components/TraversalPanel.vue'
import FlowAnimation from './components/FlowAnimation.vue'
import FlowNarrator from './components/FlowNarrator.vue'
import SupplyChainPanel from './components/SupplyChainPanel.vue'
import { sampleGraphs } from './samples/sampleGraphs.js'
import {
  store,
  initCore,
  loadGraphText,
  generateRandom,
  exportGraphText,
  notify,
} from './store/graphStore.js'

const showSamples = ref(false)
const showRandom = ref(false)

// 左侧同一时间只展开一个功能分块：点哪个，哪个的面板才出现
const sideTabs = [
  { id: 'supply', label: '物流与供应链', sub: '运力瓶颈' },
  { id: 'storage', label: '存储结构', sub: '邻接表 / 矩阵' },
  { id: 'traversal', label: '遍历 DFS / BFS', sub: '序列判别' },
  { id: 'flow', label: '最大流最小割', sub: '增广路动画' },
]
const activePanel = ref('supply')

const form = reactive({
  n: 8,
  m: 14,
  directed: 1,
  seed: 12345,
  maxCapacity: 9,
})

const graph = computed(() => store.graph)
const fileName = computed(() => store.fileName)
const backendLabel = computed(() => store.backendLabel)
const backendClass = computed(() => store.backend)
const toast = computed(() => store.toast)

let toastTimer = null

async function loadSample(sample) {
  showSamples.value = false
  await loadGraphText(sample.text, sample.name.split('（')[0])
}

async function onPickFile(event) {
  const file = event.target.files && event.target.files[0]
  event.target.value = ''
  if (!file) return
  const text = await file.text()
  await loadGraphText(text, file.name)
}

async function doRandom() {
  if (form.n < 2) {
    notify('顶点数至少是 2', 'warn')
    return
  }
  const ok = await generateRandom({
    n: Math.floor(form.n),
    m: Math.max(0, Math.floor(form.m)),
    directed: form.directed === 1,
    seed: Math.max(0, Math.floor(form.seed)),
    maxCapacity: Math.max(1, Math.floor(form.maxCapacity)),
  })
  if (ok) showRandom.value = false
}

async function saveFile() {
  const text = await exportGraphText()
  if (text == null) return
  const baseName = (store.fileName || 'saved_graph').replace(/\.txt$/i, '')
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' })
  const link = document.createElement('a')
  link.href = URL.createObjectURL(blob)
  link.download = `${baseName}.txt`
  link.click()
  URL.revokeObjectURL(link.href)
  notify('图文件已经导出，可以直接放进项目目录当输入文件', 'success')
}


watch(
  () => store.toast.seq,
  (seq) => {
    if (!seq) return
    if (toastTimer) clearTimeout(toastTimer)
    toastTimer = setTimeout(() => {
      store.toast = { ...store.toast, text: '', seq: store.toast.seq }
    }, 3600)
  },
)

onMounted(async () => {
  await initCore()
  // 一进来先载入仓库自带的 graph.txt，页面上马上有东西可看
  await loadSample(sampleGraphs[0])
})
</script>

<style scoped>
.app {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.app-header {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 10px 16px;
  border-bottom: 1px solid var(--line);
  background: linear-gradient(180deg, #121d30, #0c1524);
  box-shadow: 0 6px 20px rgba(2, 6, 16, 0.5);
  z-index: 5;
  flex-wrap: wrap;
}

.brand {
  display: flex;
  align-items: center;
  gap: 10px;
}
.brand h1 {
  font-size: 16px;
  font-weight: 650;
  letter-spacing: 0.4px;
}
.brand p {
  color: var(--text-faint);
  font-size: 12px;
}
.logo {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: linear-gradient(135deg, #22d3ee, #6366f1);
  color: #04121b;
  font-weight: 800;
  font-size: 18px;
  box-shadow: 0 6px 16px rgba(34, 211, 238, 0.25);
}

.stats {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}
.chip.file {
  color: #cfe6ff;
  border-color: #33507c;
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  display: inline-block;
  line-height: 20px;
}

.actions {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.menu-wrap {
  position: relative;
}
.menu {
  position: absolute;
  right: 0;
  top: calc(100% + 6px);
  display: flex;
  flex-direction: column;
  min-width: 250px;
  padding: 6px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--line);
  background: #131f33;
  box-shadow: var(--shadow);
  z-index: 40;
}
.menu-item {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 2px;
  padding: 7px 9px;
  border: none;
  border-radius: 7px;
  background: transparent;
  color: var(--text);
  cursor: pointer;
  text-align: left;
  font-size: 13px;
}
.menu-item:hover {
  background: #1c2b45;
}
.menu-item b {
  font-weight: 600;
}

.app-body {
  flex: 1;
  min-height: 0;
  display: flex;
  gap: 12px;
  padding: 12px 16px 16px;
}

.side {
  width: 392px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  gap: 12px;
  overflow: auto;
  padding-right: 2px;
}

/* 左侧分块切换：选了哪个，下面才展开哪个面板 */
.side-tabs {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 6px;
  padding: 6px;
  border: 1px solid var(--line);
  border-radius: var(--radius);
  background: linear-gradient(180deg, var(--panel), #0d1626);
  box-shadow: var(--shadow);
  /* 滚动内容时这排切换按钮固定在顶部 */
  position: sticky;
  top: 0;
  z-index: 3;
  flex-shrink: 0;
}
.side-tab {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 1px;
  padding: 7px 9px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--line-soft);
  background: #0e1727;
  color: var(--text-dim);
  cursor: pointer;
  text-align: left;
  transition: all 0.15s ease;
}
.side-tab .tab-name {
  font-size: 12.5px;
  font-weight: 600;
  white-space: nowrap;
}
.side-tab .tab-sub {
  font-size: 11px;
  color: var(--text-faint);
  white-space: nowrap;
}
.side-tab:hover {
  border-color: #3a527d;
  color: var(--text);
}
.side-tab.active {
  border-color: #2a7f96;
  background: linear-gradient(180deg, #14465a, #0f3446);
  color: #eaffff;
}
.side-tab.active .tab-sub {
  color: #9fdcea;
}

/* 其余面板不跟着压缩：内容超高时让 .side 自己出滚动条，
   否则 flex 会把面板压扁，再被 .panel 的 overflow: hidden 裁掉下半截 */
.side > :deep(.panel:not(.supply-panel)) {
  flex-shrink: 0;
}

/* 供应链面板：整块占满侧栏剩下的高度，
   滚动条只出现在面板内容区里，面板标题栏固定不动 */
.side > :deep(.supply-panel) {
  display: flex;
  flex-direction: column;
  min-height: 0;
}
.side > :deep(.supply-panel) > .panel-head {
  flex-shrink: 0;
}
.side > :deep(.supply-panel) > .panel-body {
  min-height: 0;
  overflow-y: auto;
}

.stage {
  flex: 1;
  min-width: 0;
}

@media (max-width: 1600px) {
  .side {
    width: 350px;
  }
  .app-body :deep(.narrator-col) {
    width: 300px;
  }
}

@media (max-width: 1180px) {
  .app-body {
    flex-direction: column;
  }
  .side {
    width: auto;
    max-height: 46vh;
  }
  .stage {
    min-height: 52vh;
  }
  .app-body :deep(.narrator-col) {
    width: auto;
    max-height: 46vh;
  }
}
</style>
