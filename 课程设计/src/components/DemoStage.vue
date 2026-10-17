   <template>
  <div v-if="store.demoOpen" class="demo">
    <header class="demo-head">
      <div class="title"><span class="dot"></span>自来水管网 · 最大流 / 最小割演示</div>
      <div class="row tight">
        <span class="hint" v-if="total">{{ index + 1 }} / {{ total }} 步</span>
        <button class="btn tiny" @click="toggle">{{ store.flowPlaying ? '暂停' : '继续' }}</button>
        <button class="btn tiny" @click="step">单步</button>
        <button class="btn tiny" @click="restart">重播</button>
        <button class="btn tiny" @click="close">退出演示</button>
      </div>
    </header>

    <main class="demo-body">
      <div class="demo-canvas"><GraphCanvas bare /></div>
      <div class="demo-strip">
        <div class="progress"><i :style="{ width: percent + '%' }"></i></div>
        <p class="say">{{ sentence }}</p>
        <div class="conclusion" v-if="conclusion" :class="{ bad: !conclusion.ok }">
          <b>{{ conclusion.big }}</b>
          <span>{{ conclusion.sub }}</span>
        </div>
      </div>
    </main>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { store } from '../store/graphStore.js'
import { waterSentence, waterConclusion } from '../scene/water.js'
import GraphCanvas from './GraphCanvas.vue'

const index = computed(() => store.flowIndex)
const total = computed(() => store.flowTotal)
const percent = computed(() =>
  total.value > 0 ? Math.round(((index.value + 1) / total.value) * 100) : 0,
)
const sentence = computed(() => waterSentence(store.flowFrame) || '准备开始…')
const conclusion = computed(() =>
  total.value > 0 && index.value >= total.value - 1 ? waterConclusion(store.flowResult) : null,
)

function toggle() {
  const c = store.flowControls
  if (!c) return
  if (store.flowPlaying) c.pause()
  else c.play()
}

function step() {
  const c = store.flowControls
  if (c) c.step()
}

function restart() {
  const c = store.flowControls
  if (!c) return
  c.reset()
  c.play()
}

function close() {
  store.demoOpen = false
}
</script>

<style scoped>
.demo {
  position: fixed;
  inset: 0;
  z-index: 70;
  display: flex;
  flex-direction: column;
  background: radial-gradient(1200px 700px at 30% -10%, #14263c 0%, #070d18 60%), #070d18;
}

.demo-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 18px;
  border-bottom: 1px solid var(--line);
  background: linear-gradient(180deg, #16233a, #111c2e);
  flex-wrap: wrap;
}
.title {
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 17px;
  font-weight: 650;
  letter-spacing: 0.4px;
}
.title .dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
  background: var(--cyan);
  box-shadow: 0 0 12px var(--cyan);
}

.demo-body {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 0;
}
.demo-canvas {
  flex: 1;
  min-height: 0;
  display: flex;
}
.demo-canvas :deep(.stage-card) {
  flex: 1;
  border-radius: 0;
  border-left: none;
  border-right: none;
  border-top: none;
  box-shadow: none;
}
.demo-canvas :deep(.canvas-host) {
  min-height: 260px;
}

.demo-strip {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 12px 24px 16px;
  border-top: 1px solid var(--line);
  background: linear-gradient(180deg, #0d1626, #0a1220);
}
.progress {
  height: 4px;
  border-radius: 999px;
  background: #16233a;
  overflow: hidden;
}
.progress i {
  display: block;
  height: 100%;
  border-radius: 999px;
  background: linear-gradient(90deg, #22d3ee, #818cf8);
  transition: width 0.2s ease;
}
.say {
  font-size: 20px;
  line-height: 1.5;
  color: #e8f2ff;
  min-height: 30px;
}
.conclusion {
  display: flex;
  flex-wrap: wrap;
  align-items: baseline;
  gap: 12px;
  padding: 10px 14px;
  border-radius: var(--radius-sm);
  border: 1px solid #1f6f5c;
  background: rgba(52, 211, 153, 0.1);
}
.conclusion.bad {
  border-color: #7f3b3b;
  background: rgba(248, 113, 113, 0.1);
}
.conclusion b {
  font-size: 24px;
  color: var(--green);
}
.conclusion.bad b {
  color: var(--red);
}
.conclusion span {
  color: var(--text-dim);
  font-size: 13px;
}
</style>
