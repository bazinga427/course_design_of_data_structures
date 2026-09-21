<template>
  <div class="panel">
    <div class="panel-head">
      <h3>存储结构</h3>
      <div class="row tight">
        <button class="btn tiny" :class="{ active: tab === 'list' }" @click="tab = 'list'">
          邻接表
        </button>
        <button class="btn tiny" :class="{ active: tab === 'matrix' }" @click="tab = 'matrix'">
          邻接矩阵
        </button>
        <button class="btn tiny" :class="{ active: tab === 'edges' }" @click="tab = 'edges'">
          边表
        </button>
      </div>
    </div>

    <div class="panel-body">
      <p v-if="!graph" class="hint">还没有图，先在上面载入或生成一张。</p>

      <template v-else>
        <p class="hint">
          {{ graph.directed ? '有向图' : '无向图' }}：邻接表一共 {{ graph.n }} 行，
          {{ graph.directed ? '每条边只存一个方向' : '每条边两个方向各存一次' }}；
          邻接矩阵 / 容量矩阵里的数字就是边的容量。
        </p>

        <!-- 邻接表 -->
        <div v-if="tab === 'list'" class="list-wrap">
          <div
            v-for="u in graph.n"
            :key="u"
            class="list-row"
            :class="{ mark: u === store.selectedNode }"
            @click="store.selectedNode = u"
          >
            <span class="node-tag">{{ u }}</span>
            <span class="mono grow">
              {{ rows[u] && rows[u].length ? rows[u].join(' → ') : '（没有出边）' }}
            </span>
            <span class="hint">{{ rows[u] ? rows[u].length : 0 }} 条</span>
          </div>
        </div>

        <!-- 邻接矩阵 -->
        <div v-else-if="tab === 'matrix'">
          <p v-if="graph.matrixOmitted" class="hint warn">
            点数超过 {{ coreInfo && coreInfo.matrixLimit ? coreInfo.matrixLimit : 150 }}，
            矩阵太大就不画了（算法照样算，只是不在页面上铺表格）。
          </p>
          <div v-else class="table-wrap">
            <table class="grid">
              <thead>
                <tr>
                  <th class="corner">行\列</th>
                  <th v-for="v in graph.n" :key="v" :class="{ mark: v === store.sink }">
                    {{ v }}
                  </th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="u in graph.n" :key="u">
                  <td class="rowhead" :class="{ mark: u === store.source }">{{ u }}</td>
                  <td
                    v-for="v in graph.n"
                    :key="v"
                    :class="cellClass(u, v)"
                  >
                    {{ cellText(u, v) }}
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
          <p class="hint">
            绿色是已经流过去的流量（写成 流量/容量），粉色是割边，青色行列是当前的源点 / 汇点。
          </p>
        </div>

        <!-- 边表 -->
        <div v-else class="table-wrap">
          <table class="grid edge-table">
            <thead>
              <tr>
                <th>#</th>
                <th>u</th>
                <th>v</th>
                <th>容量</th>
                <th v-if="flowMap.size">流量</th>
                <th v-if="flowMap.size">割边</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(e, i) in graph.edges" :key="`${e.u}-${e.v}`">
                <td>{{ i + 1 }}</td>
                <td>{{ e.u }}</td>
                <td>{{ e.v }}</td>
                <td>{{ e.cap }}</td>
                <template v-if="flowMap.size">
                  <td class="flow">{{ flowOf(e.u, e.v) }}</td>
                  <td :class="{ cut: cutSet.has(`${e.u}>${e.v}`) }">
                    {{ cutSet.has(`${e.u}>${e.v}`) ? '是' : '' }}
                  </td>
                </template>
              </tr>
            </tbody>
          </table>
        </div>
      </template>
    </div>
  </div>
</template>

<script setup>
import { computed, ref } from 'vue'
import { store } from '../store/graphStore.js'

const tab = ref('list')

const graph = computed(() => store.graph)
const coreInfo = computed(() => store.coreInfo)
const rows = computed(() => (graph.value ? graph.value.adjList : []))

// 最大流算完之后，矩阵和边表顺手把流量一起显示出来
const flowMap = computed(() => {
  const map = new Map()
  const res = store.flowResult
  if (res && res.edgeFlow) {
    for (const e of res.edgeFlow) map.set(`${e.u}>${e.v}`, e.flow)
  }
  return map
})
const cutSet = computed(() => {
  const set = new Set()
  const res = store.flowResult
  if (res && res.cutEdges) for (const [u, v] of res.cutEdges) set.add(`${u}>${v}`)
  return set
})

function rawFlow(u, v) {
  return flowMap.value.get(`${u}>${v}`)
}

function flowOf(u, v) {
  const value = rawFlow(u, v)
  if (value === undefined) return 0
  return Math.max(0, value)
}

const capOf = (u, v) => (graph.value.matrix ? graph.value.matrix[u][v] : 0)

function cellText(u, v) {
  if (u === 0 || v === 0) return ''
  const cap = capOf(u, v)
  const flow = rawFlow(u, v)
  if (flow !== undefined && cap > 0 && flow > 0) return `${Math.max(0, flow)}/${cap}`
  return cap === 0 ? '·' : String(cap)
}

function cellClass(u, v) {
  const cap = capOf(u, v)
  const flow = rawFlow(u, v)
  const classes = []
  if (cap === 0) classes.push('empty')
  if (cutSet.value.has(`${u}>${v}`)) classes.push('cut')
  else if (flow !== undefined && flow > 0 && cap > 0) classes.push('flow')
  return classes
}
</script>

<style scoped>
.list-wrap {
  display: flex;
  flex-direction: column;
  gap: 4px;
  max-height: 240px;
  overflow: auto;
}
.list-row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 8px;
  border-radius: 7px;
  border: 1px solid transparent;
  background: #0e1727;
  cursor: pointer;
}
.list-row:hover {
  border-color: var(--line);
}
.list-row.mark {
  border-color: var(--cyan);
  background: rgba(34, 211, 238, 0.1);
}
.node-tag {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 24px;
  height: 22px;
  border-radius: 6px;
  background: #1b2942;
  border: 1px solid #33507c;
  color: #cfe6ff;
  font-size: 12px;
  font-weight: 600;
}
.edge-table {
  width: 100%;
}
.edge-table td.flow {
  color: var(--green);
}
</style>
