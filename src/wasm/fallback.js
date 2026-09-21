// ============================================================
//  fallback.js —— C++ 核心的 JavaScript 等价实现（兜底用）
//
//  为什么要有这个文件：
//    WASM 要装 Emscripten 才能编译，前端同学在没有 WASM 的时候也得能开发、
//    能演示。这里用 JS 把同一套算法再实现一遍，接口和 binding.cpp 一模一样，
//    所以 loader.js 换一个实现，其它代码一行都不用改。
//
//  重要：算法顺序刻意和 graph.cpp / flow.cpp 保持一致 ——
//    * DFS：递归 + 邻接表里的先后顺序（等价于 get_dfs_sequence）
//    * BFS：队列，先入先出（等价于 get_bfs_sequence）
//    * 最大流：Ford-Fulkerson，用 DFS 找增广路，残余容量按 1..n 顺序扫
//  只有一个地方必然不同：随机图的随机数发生器。C++ 用 mt19937，JS 侧用
//  mulberry32，同一 seed 在“网页内部”可复现，但和 C++ 生成的图不会逐条相同。
//  要严格对齐，编译出 WASM 即可（WASM 走的就是真正的 C++ 代码）。
// ============================================================

const MAX_NODES = 2000
const MATRIX_LIMIT = 150
const SAFETY_LIMIT = 200000

export const FALLBACK_INFO = {
  name: 'graph_core',
  version: '1.0.0-js',
  maxNodes: MAX_NODES,
  matrixLimit: MATRIX_LIMIT,
}

function mulberry32(seed) {
  let a = seed >>> 0
  return function next() {
    a = (a + 0x6d2b79f5) >>> 0
    let t = a
    t = Math.imul(t ^ (t >>> 15), t | 1)
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61)
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

// 把一行里的数字全部读出来当参数用。这样一来：
//   * 空格、Tab、逗号、分号 都能当分隔符（"1,2,3" 和 "1 2 3" 一样）
//   * 行尾的注释（# 或 % 或 //）会被丢掉，记事本里加的中文说明不会把文件读坏
// 返回读到的数字数组，空行 / 纯注释行会返回空数组。
function numbersIn(line) {
  const cut = line.search(/[#%]|\/\//)
  const body = cut >= 0 ? line.slice(0, cut) : line
  const out = []
  const re = /-?\d+/g
  let hit = re.exec(body)
  while (hit) {
    out.push(Number(hit[0]))
    hit = re.exec(body)
  }
  return out
}

// 这一份和 graph_io.cpp 里的 load_graph 是同一套规则，改一个要改两个。
const LOAD_RULES_TIP =
  '支持一行 n m is_directed（顶点数 边数 是否有向），后面每行一条边 u v [容量]；' +
  '编号 1 开始和 0 开始都认，逗号 / 空格 / Tab 都能当分隔符，# 开头的行当注释。'

export function createFallbackCore() {
  let n = 0
  let m = 0
  let directed = 1
  let adj = [] // adj[u] = [v, ...]
  let cap = [] // cap[u][v] = u->v 的容量，0 表示没有这条边

  // ----------------------------------------------------------
  //  建图
  // ----------------------------------------------------------
  function initGraph(nodeCount, isDirected) {
    n = nodeCount
    directed = isDirected ? 1 : 0
    m = 0
    adj = Array.from({ length: n + 1 }, () => [])
    cap = Array.from({ length: n + 1 }, () => new Array(n + 1).fill(0))
  }

  function addEdge(u, v, capacity) {
    if (u < 1 || u > n || v < 1 || v > n) return
    adj[u].push(v)
    cap[u][v] = capacity
    m += 1
    if (!directed) {
      adj[v].push(u)
      cap[v][u] = capacity
    }
  }

  // 等价于 graph_io::load_graph（读文件建图）
  //
  // 比“照着第一行 split 一下”麻烦的地方，都是为了能打开别人给的各种图文件：
  //   * 第一行前面可能有空行、注释、UTF-8 BOM（记事本另存为 UTF-8 就会加）
  //   * 编号可能从 0 开始（很多教材、以及早期版本的 graph.txt 都这样）
  //   * 有的文件一行写 "1 2 3"，有的写 "1,2,3"，有的把容量省了
  // 0 开始编号的文件会自动 +1 映射成 1..n，这样网页里所有地方（邻接表、
  // 遍历序列、最大流）都还是 1..n 一套编号，不用到处判分支。
  function loadText(text, fileName) {
    let body = text == null ? '' : String(text)
    if (body.charCodeAt(0) === 0xfeff) body = body.slice(1) // 去掉 BOM
    if (!body.trim()) return { ok: false, error: '文件是空的，没有内容可以解析' }

    const lines = body.split(/\r?\n/)
    let header = null
    let headerLine = 0
    let nodeCount = 0
    let edgeCount = 0
    let isDirected = 1
    const raw = [] // { u, v, cap, line }

    for (let i = 0; i < lines.length; i += 1) {
      const nums = numbersIn(lines[i])
      if (!nums.length) continue // 空行 / 注释行

      if (!header) {
        headerLine = i + 1
        if (nums.length < 2) {
          return {
            ok: false,
            error: `第 ${headerLine} 行应该是：n m is_directed（顶点数 边数 是否有向）。${LOAD_RULES_TIP}`,
          }
        }
        header = nums
        nodeCount = nums[0]
        edgeCount = nums[1]
        // 第三个数不写就是有向图（和 C++ 核心的默认值一致）
        isDirected = nums.length > 2 ? (nums[2] ? 1 : 0) : 1
        continue
      }

      if (raw.length >= edgeCount) continue // 第一行写了多少条就读多少条
      if (nums.length < 2) {
        return { ok: false, error: `第 ${i + 1} 行：一条边至少要写两个点编号（u v [容量]）` }
      }
      let c = nums.length > 2 ? nums[2] : 1
      if (!Number.isFinite(c) || c <= 0) c = 1
      raw.push({ u: nums[0], v: nums[1], cap: c, line: i + 1 })
    }

    if (!header) {
      return { ok: false, error: `第一行应该是：n m is_directed（顶点数 边数 是否有向）。${LOAD_RULES_TIP}` }
    }
    if (nodeCount < 1) return { ok: false, error: '顶点数 n 必须是正数' }
    if (nodeCount > MAX_NODES) {
      return { ok: false, error: `顶点数超出上限，本程序最多支持 ${MAX_NODES} 个点` }
    }
    if (edgeCount < 0) return { ok: false, error: '边数 m 不能是负数' }
    if (raw.length < edgeCount) {
      return {
        ok: false,
        error:
          `文件里只读到 ${raw.length} 条边，第 ${headerLine} 行却写了 ${edgeCount} 条。` +
          '（把第一行的 m 改成实际边数，或者把缺的边补上）',
      }
    }

    // 编号基准：只要出现了 0 号点，就当成 0 开始编号。
    // 和 graph_io.cpp 里的 is_zero_based 同一个口径：0 开始编号时 n 是点数，
    // 编号范围 0..n-1；也容忍把 n 写成“最大编号”的文件（那样点数就是 n+1）。
    const zeroBased = raw.some((e) => e.u === 0 || e.v === 0)
    const minOk = zeroBased ? 0 : 1
    const bad = raw.find(
      (e) => e.u < minOk || e.u > nodeCount || e.v < minOk || e.v > nodeCount,
    )
    if (bad) {
      const range = zeroBased ? `0..${nodeCount - 1}（或 0..${nodeCount}）` : `1..${nodeCount}`
      return {
        ok: false,
        error:
          `第 ${bad.line} 行的边 ${bad.u} ${bad.v} 编号超出范围：` +
          `第 ${headerLine} 行写的是 n = ${nodeCount}，编号只能是 ${range}。`,
      }
    }

    const shift = zeroBased ? 1 : 0
    // 0 开始编号 + n 写成了最大编号时（例如 "4 ..." 里出现 4 号点），点数其实是 n + 1
    let size = nodeCount
    if (zeroBased) {
      for (const e of raw) size = Math.max(size, e.u + 1, e.v + 1)
    }

    initGraph(size, isDirected === 1)
    for (const e of raw) addEdge(e.u + shift, e.v + shift, e.cap)
    return { ok: true, fileName: fileName || '', n, m, directed, base: zeroBased ? 0 : 1 }
  }

  // 等价于 Graph::build_random
  function random(nodeCount, edgeCount, isDirected, seed, maxCapacity) {
    if (!Number.isFinite(nodeCount) || nodeCount < 1 || nodeCount > MAX_NODES) {
      return { ok: false, error: `顶点数必须在 1..${MAX_NODES} 之间` }
    }
    if (!Number.isFinite(edgeCount) || edgeCount < 0) {
      return { ok: false, error: '边数不能是负数' }
    }
    const capacity = maxCapacity >= 1 ? Math.floor(maxCapacity) : 1

    const limit = isDirected ? nodeCount * (nodeCount - 1) : (nodeCount * (nodeCount - 1)) / 2
    const wanted = Math.min(edgeCount, limit)

    initGraph(nodeCount, isDirected === 1 || isDirected === true)

    const usedSeed = seed > 0 ? seed : Math.floor(Math.random() * 0xffffffff)
    const rnd = mulberry32(usedSeed)
    const randCap = () => (capacity > 1 ? 1 + Math.floor(rnd() * capacity) : 1)

    if (n >= 2 && wanted > 0) {
      if (wanted > limit / 2) {
        // 稠密图：把所有可能的边列出来打乱，取前 wanted 条
        const all = []
        for (let u = 1; u <= n; u += 1) {
          for (let v = 1; v <= n; v += 1) {
            if (u === v) continue
            if (!directed && v < u) continue
            all.push([u, v])
          }
        }
        for (let i = all.length - 1; i > 0; i -= 1) {
          const j = Math.floor(rnd() * (i + 1))
          const tmp = all[i]
          all[i] = all[j]
          all[j] = tmp
        }
        for (let i = 0; i < wanted; i += 1) addEdge(all[i][0], all[i][1], randCap())
      } else {
        // 稀疏图：随机抽两个点当边，抽重了再抽
        let guard = 0
        while (m < wanted && guard < wanted * 400 + 4000) {
          guard += 1
          const u = 1 + Math.floor(rnd() * n)
          const v = 1 + Math.floor(rnd() * n)
          if (u === v) continue
          if (cap[u][v] !== 0) continue
          if (!directed && cap[v][u] !== 0) continue
          addEdge(u, v, randCap())
        }
      }
    }

    return { ok: true, n, m, directed, seed: usedSeed, maxCapacity: capacity }
  }

  // ----------------------------------------------------------
  //  公共小工具
  // ----------------------------------------------------------
  // 等价于 binding.cpp 里的 collect_edges：无向图只留一份，顺序按 u,v 排好
  function collectEdges() {
    const seen = new Set()
    const edges = []
    for (let u = 1; u <= n; u += 1) {
      for (const v of adj[u]) {
        if (v < 1 || v > n) continue
        if (!directed && v < u) continue
        const key = `${u}_${v}`
        if (seen.has(key)) continue
        seen.add(key)
        const c = cap[u][v] > 0 ? cap[u][v] : 1
        edges.push({ u, v, cap: c })
      }
    }
    edges.sort((a, b) => (a.u !== b.u ? a.u - b.u : a.v - b.v))
    return edges
  }

  function snapshot() {
    const edges = collectEdges()
    const showMatrix = n <= MATRIX_LIMIT
    const matrix = showMatrix
      ? Array.from({ length: n + 1 }, (_, u) =>
          Array.from({ length: n + 1 }, (_, v) => (cap[u] && cap[u][v]) || 0),
        )
      : null

    return {
      ok: true,
      n,
      m: edges.length,
      directed,
      nodes: Array.from({ length: n }, (_, i) => i + 1),
      edges,
      adjList: Array.from({ length: n + 1 }, (_, u) => (adj[u] ? adj[u].slice() : [])),
      matrixOmitted: !showMatrix,
      matrix,
    }
  }

  // 等价于 graph_io::save_graph（写成和 graph.txt 一样的格式）
  function saveText() {
    const edges = collectEdges()
    let out = `${n} ${edges.length} ${directed}\n`
    for (const e of edges) out += `${e.u} ${e.v} ${e.cap}\n`
    return { ok: true, text: out }
  }

  // ----------------------------------------------------------
  //  遍历
  // ----------------------------------------------------------
  function dfsSequence(startNode) {
    const visited = new Array(n + 1).fill(false)
    const parent = new Array(n + 1).fill(0)
    const seq = []
    const walk = (u) => {
      visited[u] = true
      seq.push(u)
      for (const v of adj[u]) {
        if (!visited[v]) {
          parent[v] = u
          walk(v)
        }
      }
    }
    walk(startNode)
    return { seq, parent }
  }

  function bfsSequence(startNode) {
    const visited = new Array(n + 1).fill(false)
    const parent = new Array(n + 1).fill(0)
    const seq = []
    const queue = [startNode]
    visited[startNode] = true
    for (let head = 0; head < queue.length; head += 1) {
      const u = queue[head]
      seq.push(u)
      for (const v of adj[u]) {
        if (!visited[v]) {
          visited[v] = true
          parent[v] = u
          queue.push(v)
        }
      }
    }
    return { seq, parent }
  }

  function reachableFrom(startNode) {
    const visited = new Array(n + 1).fill(false)
    const result = []
    const queue = [startNode]
    visited[startNode] = true
    for (let head = 0; head < queue.length; head += 1) {
      const u = queue[head]
      result.push(u)
      for (const v of adj[u]) {
        if (!visited[v]) {
          visited[v] = true
          queue.push(v)
        }
      }
    }
    return result.sort((a, b) => a - b)
  }

  function traverse(mode, start) {
    if (start < 1 || start > n) {
      return { ok: false, error: `起点必须在 1..${n} 之间` }
    }
    const { seq, parent } = mode === 1 ? bfsSequence(start) : dfsSequence(start)
    const parentEdges = []
    for (let v = 1; v <= n; v += 1) {
      if (parent[v] !== 0) parentEdges.push([parent[v], v])
    }
    return {
      ok: true,
      mode: mode === 1 ? 1 : 0,
      start,
      seq,
      reachable: reachableFrom(start),
      parentEdges,
    }
  }

  // ----------------------------------------------------------
  //  遍历序列合法性判别（等价于 is_valid_dfs / is_valid_bfs）
  // ----------------------------------------------------------
  function hasUnvisitedNeighbor(u, visited) {
    for (const v of adj[u]) if (!visited[v]) return true
    return false
  }

  function isValidDfs(seq, startNode) {
    if (!seq.length || seq[0] !== startNode) return false
    if (startNode < 1 || startNode > n) return false

    const visited = new Array(n + 1).fill(false)
    const stack = []
    visited[startNode] = true
    stack.push(startNode)

    for (let i = 1; i < seq.length; i += 1) {
      const v = seq[i]
      if (v < 1 || v > n || visited[v]) return false
      while (stack.length && !hasUnvisitedNeighbor(stack[stack.length - 1], visited)) {
        stack.pop()
      }
      if (!stack.length) return false
      if (cap[stack[stack.length - 1]][v] === 0) return false
      visited[v] = true
      stack.push(v)
    }

    return seq.length === reachableFrom(startNode).length
  }

  function isValidBfs(seq, startNode) {
    if (!seq.length || seq[0] !== startNode) return false
    if (startNode < 1 || startNode > n) return false

    const level = new Array(n + 1).fill(-1)
    const parentPos = new Array(n + 1).fill(-1)
    level[startNode] = 0

    for (let i = 1; i < seq.length; i += 1) {
      const v = seq[i]
      if (v < 1 || v > n || level[v] !== -1) return false

      let bestLevel = -1
      let bestPos = -1
      for (let j = 0; j < i; j += 1) {
        const u = seq[j]
        if (cap[u][v] === 0) continue
        if (bestPos === -1 || level[u] < bestLevel) {
          bestLevel = level[u]
          bestPos = j
        }
      }
      if (bestPos === -1) return false
      level[v] = bestLevel + 1
      parentPos[v] = bestPos

      if (level[v] < level[seq[i - 1]]) return false
      for (let j = 1; j < i; j += 1) {
        const u = seq[j]
        if (level[u] === level[v] && parentPos[u] > parentPos[v]) return false
      }
    }

    return seq.length === reachableFrom(startNode).length
  }

  function checkSequence(start, seqText) {
    if (start < 1 || start > n) {
      return { ok: false, error: `起点必须在 1..${n} 之间` }
    }
    const seq = String(seqText || '')
      .trim()
      .split(/\s+/)
      .filter(Boolean)
      .map(Number)
      .filter((x) => Number.isFinite(x))
    if (!seq.length) return { ok: false, error: '请先输入要判别的序列，例如：1 2 3 4' }

    return {
      ok: true,
      start,
      seq,
      dfs: isValidDfs(seq, start),
      bfs: isValidBfs(seq, start),
      sampleDfs: dfsSequence(start).seq,
      sampleBfs: bfsSequence(start).seq,
    }
  }

  // ----------------------------------------------------------
  //  最大流 / 最小割（Ford-Fulkerson，等价于 flow.cpp）
  // ----------------------------------------------------------
  function maxFlow(s, t) {
    if (n < 2) return { ok: false, error: '至少要有两个点才能算最大流' }
    if (s < 1 || s > n || t < 1 || t > n) {
      return { ok: false, error: `源点和汇点都必须在 1..${n} 之间` }
    }
    if (s === t) return { ok: false, error: '源点和汇点不能是同一个点' }

    const residual = cap.map((row) => row.slice())
    const steps = []
    let value = 0

    // 在残余网络里用 DFS 找一条 s -> t 的路，扫的顺序和 C++ 一样是 1..n
    const findPath = (u, tt, visited, path) => {
      if (u === tt) return true
      visited[u] = true
      for (let v = 1; v <= n; v += 1) {
        if (residual[u][v] > 0 && !visited[v]) {
          path.push(v)
          if (findPath(v, tt, visited, path)) return true
          path.pop()
        }
      }
      return false
    }

    while (steps.length < SAFETY_LIMIT) {
      const visited = new Array(n + 1).fill(false)
      const path = [s]
      if (!findPath(s, t, visited, path)) break

      let bottleneck = -1
      for (let i = 0; i + 1 < path.length; i += 1) {
        const c = residual[path[i]][path[i + 1]]
        if (bottleneck < 0 || c < bottleneck) bottleneck = c
      }
      if (bottleneck <= 0) break

      for (let i = 0; i + 1 < path.length; i += 1) {
        const u = path[i]
        const v = path[i + 1]
        residual[u][v] -= bottleneck
        residual[v][u] += bottleneck
      }

      value += bottleneck
      steps.push({ path: path.slice(), bottleneck, flowAfter: value })
    }

    // 实际流量 = 原始容量 - 残余容量（和 C++ 的口径一致，可能有负数）
    const flow = Array.from({ length: n + 1 }, (_, u) =>
      Array.from({ length: n + 1 }, (_, v) => cap[u][v] - residual[u][v]),
    )

    // 最小割：残余网络里从 s 还能走到的点就是 S 侧
    const inSource = new Array(n + 1).fill(false)
    const queue = [s]
    inSource[s] = true
    for (let head = 0; head < queue.length; head += 1) {
      const u = queue[head]
      for (let v = 1; v <= n; v += 1) {
        if (residual[u][v] > 0 && !inSource[v]) {
          inSource[v] = true
          queue.push(v)
        }
      }
    }

    const sourceSide = []
    const sinkSide = []
    for (let i = 1; i <= n; i += 1) (inSource[i] ? sourceSide : sinkSide).push(i)

    const cutEdges = []
    let cutCapacity = 0
    for (let u = 1; u <= n; u += 1) {
      if (!inSource[u]) continue
      for (let v = 1; v <= n; v += 1) {
        if (inSource[v] || cap[u][v] <= 0) continue
        cutEdges.push([u, v])
        cutCapacity += cap[u][v]
      }
    }

    const edgeFlow = collectEdges().map(({ u, v }) => ({
      u,
      v,
      cap: cap[u][v],
      flow: flow[u][v],
    }))

    return {
      ok: true,
      s,
      t,
      value,
      cutCapacity,
      correct: cutCapacity === value,
      steps,
      sourceSide,
      sinkSide,
      cutEdges,
      edgeFlow,
    }
  }

  return {
    loadText,
    random,
    saveText,
    snapshot,
    traverse,
    checkSequence,
    maxFlow,
    // 只有测试脚本会用到的“内部”出口
    _debug: () => ({ n, m, directed, adj: adj.map((row) => row.slice()), cap }),
  }
}
