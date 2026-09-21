// ============================================================
//  loader.js —— 前端统一通过它拿“图算法接口”
//
//  优先用 C++ 编译出来的 WASM（public/wasm/graph_core.js）；
//  如果那份文件还不存在（比如还没装 Emscripten），自动退回 src/wasm/fallback.js
//  里的 JS 版本。两条路的函数名、返回字段完全一样，所以上层组件不用关心
//  现在跑的是哪一个。
//
//  WASM 侧的接口契约见 binding.cpp 和 FRONTEND.md。
// ============================================================

import { createFallbackCore, FALLBACK_INFO } from './fallback.js'

const WASM_FILE = 'wasm/graph_core.js'

function baseUrl() {
  const base = import.meta.env.BASE_URL || '/'
  return new URL(base, document.baseURI)
}

// 把 C++ 返回的一段 JSON 文本变成 JS 对象；出错也不会让页面白屏
function makeCaller(module) {
  return function call(fn, argTypes, args) {
    let raw
    try {
      raw = module.ccall(fn, 'string', argTypes, args)
    } catch (err) {
      return { ok: false, error: `调用 ${fn} 失败：${(err && err.message) || err}` }
    }
    try {
      return JSON.parse(raw)
    } catch (err) {
      return { ok: false, error: `${fn} 返回的不是合法 JSON` }
    }
  }
}

function makeWasmApi(module) {
  const call = makeCaller(module)
  return {
    backend: 'wasm',
    backendLabel: 'C++ / WASM 核心',
    info: FALLBACK_INFO,
    version: () => call('gv_version', [], []),
    loadText: (text, fileName = '') => call('gv_load_text', ['string', 'string'], [text, fileName]),
    random: (n, m, directed, seed, maxCapacity) =>
      call(
        'gv_random',
        ['number', 'number', 'number', 'number', 'number'],
        [n, m, directed, seed, maxCapacity],
      ),
    saveText: () => call('gv_save_text', [], []),
    snapshot: () => call('gv_snapshot', [], []),
    traverse: (mode, start) => call('gv_traverse', ['number', 'number'], [mode, start]),
    checkSequence: (start, seqText) =>
      call('gv_check_sequence', ['number', 'string'], [start, String(seqText ?? '')]),
    maxFlow: (s, t) => call('gv_max_flow', ['number', 'number'], [s, t]),
  }
}

function makeFallbackApi() {
  const core = createFallbackCore()
  const guard = (fn) => (...args) => {
    try {
      return fn(...args)
    } catch (err) {
      return { ok: false, error: `JS 兜底实现出错：${(err && err.message) || err}` }
    }
  }
  return {
    backend: 'js',
    backendLabel: 'JS 兜底实现（未编译 WASM）',
    info: FALLBACK_INFO,
    version: () => ({ ok: true, ...FALLBACK_INFO }),
    loadText: guard((text, fileName = '') => core.loadText(text, fileName)),
    random: guard((n, m, directed, seed, maxCapacity) =>
      core.random(n, m, directed, seed, maxCapacity),
    ),
    saveText: guard(() => core.saveText()),
    snapshot: guard(() => core.snapshot()),
    traverse: guard((mode, start) => core.traverse(mode, start)),
    checkSequence: guard((start, seqText) => core.checkSequence(start, seqText)),
    maxFlow: guard((s, t) => core.maxFlow(s, t)),
  }
}

let cached = null

/**
 * 加载算法接口。
 * @param {{ preferWasm?: boolean, verbose?: boolean }} options
 * @returns {Promise<object>} 见上面 makeWasmApi / makeFallbackApi 的字段
 */
export async function loadGraphApi(options = {}) {
  const { preferWasm = true, verbose = false } = options
  if (cached) return cached

  if (preferWasm) {
    try {
      const url = new URL(WASM_FILE, baseUrl()).href
      const mod = await import(/* @vite-ignore */ url)
      const factory = mod.default || mod.createGraphCore
      if (typeof factory !== 'function') throw new Error('graph_core.js 里没有找到模块工厂函数')

      const module = await factory({
        locateFile: (path) => new URL(`wasm/${path}`, baseUrl()).href,
        print: verbose ? (text) => console.log('[graph_core]', text) : () => {},
        printErr: (text) => console.warn('[graph_core]', text),
      })
      cached = makeWasmApi(module)
      if (verbose) console.log('[graph] 已加载', url)
      return cached
    } catch (err) {
      console.warn(
        '[graph] 没有加载到 WASM（先用 JS 兜底实现跑）：',
        (err && err.message) || err,
      )
    }
  }

  cached = makeFallbackApi()
  return cached
}

/** 只给测试脚本用：每次都重新建一个 JS 兜底实现 */
export function createDetachedFallback() {
  return makeFallbackApi()
}
