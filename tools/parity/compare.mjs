// ============================================================
//  tools/parity/compare.mjs —— 一致性自检
//
//  用原生 g++ 跑一遍真正的 C++ 核心（binding.cpp + graph.cpp + flow.cpp
//  + graph_io.cpp），再跑一遍 src/wasm/fallback.js，把两边每个接口的返回
//  值逐字段对比。两边一致，说明“网页上的结果 = EasyX 版的结果”。
//
//  用法（在项目根目录）：node tools/parity/compare.mjs
//  可选：用环境变量 PARITY_ROOT 指定项目根，用 CXX 指定编译器
//  也可以先自己跑出 dump 再对比：node tools/parity/compare.mjs cpp_dump.txt
// ============================================================

import { execFileSync } from 'node:child_process'
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { createFallbackCore } from '../../src/wasm/fallback.js'

const here = dirname(fileURLToPath(import.meta.url))
const projectRoot = resolve(process.env.PARITY_ROOT || join(here, '..', '..'))
const cxx = process.env.CXX || 'g++'
const dumpArgument = process.argv[2]

// 和 tools/build_wasm.bat 一个规则：优先 cpp\ 目录，其次项目根
let srcDir = join(projectRoot, 'cpp')
if (!existsSync(join(srcDir, 'graph.cpp'))) srcDir = projectRoot

let dump
if (dumpArgument) {
  console.log(`[1/3] 直接使用已有的 C++ 输出：${dumpArgument}`)
  dump = readFileSync(resolve(dumpArgument), 'utf8')
} else {
  // 算法源码（graph.cpp / flow.cpp / graph_io.cpp）放在 cpp\ 或项目根；
  // 网页适配层 binding.cpp 在项目根，也可能被一起放进 cpp\。
  const sources = [
    existsSync(join(srcDir, 'binding.cpp'))
      ? join(srcDir, 'binding.cpp')
      : join(projectRoot, 'binding.cpp'),
    join(srcDir, 'graph.cpp'),
    join(srcDir, 'flow.cpp'),
    join(srcDir, 'graph_io.cpp'),
  ]
  for (const file of sources) {
    if (!existsSync(file)) {
      console.error(`[x] 找不到 ${file}`)
      process.exit(2)
    }
  }

  // 编译产物放在项目根下的临时目录里（.gitignore 已忽略）
  const workDir = mkdtempSync(join(projectRoot, '.parity-build-'))
  const exePath = join(workDir, process.platform === 'win32' ? 'parity.exe' : 'parity')
  try {
    console.log(`[1/3] 编译 C++ 核心：${cxx}`)
    execFileSync(
      cxx,
      ['-std=c++17', '-O2', '-I', srcDir, ...sources, join(here, 'harness.cpp'), '-o', exePath],
      { stdio: ['ignore', 'inherit', 'inherit'] },
    )

    console.log(
      '[2/3] 运行 C++ 版本（工作目录＝项目根，读 graph.txt / graph2.txt / rev.txt / graph0.txt）',
    )
    dump = execFileSync(exePath, [], {
      cwd: projectRoot,
      encoding: 'utf8',
      maxBuffer: 64 * 1024 * 1024,
    })
  } catch (err) {
    console.error(
      `[x] 编译或运行失败：${(err && err.message) || err}\n` +
        '    如果你的环境不允许脚本启动子进程，可以先手动跑：\n' +
        `      ${cxx} -std=c++17 -O2 -I "${srcDir}" binding.cpp graph.cpp flow.cpp graph_io.cpp tools/parity/harness.cpp -o parity.exe\n` +
        '      parity.exe > cpp_dump.txt\n' +
        '      node tools/parity/compare.mjs cpp_dump.txt',
    )
    process.exit(2)
  } finally {
    rmSync(workDir, { recursive: true, force: true })
  }
}

{
  const lines = dump.split(/\r?\n/)
  const cpp = new Map()
  for (let i = 0; i < lines.length; i += 1) {
    if (!lines[i].startsWith('##CALL ')) continue
    const label = lines[i].slice('##CALL '.length).trim()
    try {
      cpp.set(label, JSON.parse(lines[i + 1]))
    } catch {
      console.log(`! 解析 C++ 输出失败：${label}`)
    }
  }

  console.log('[3/3] 用 JS 兜底实现跑同样的调用，逐字段对比')
  const core = createFallbackCore()
  // graph0.txt 用的是 0 开始编号：两边都必须自动映射成 1..n，结果要完全一致
  const files = ['graph.txt', 'graph2.txt', 'rev.txt', 'graph0.txt']
  const sequences = ['1 2 3 4 5 6', '1 3 2 5 6 4', '1 2 4 6 3 5', '6 5 4 3 2 1', '1 1 2 3']

  let checked = 0
  const diffs = []

  const normalize = (value) => {
    if (typeof value === 'string') return value.replace(/\r\n/g, '\n')
    if (Array.isArray(value)) return value.map(normalize)
    if (value && typeof value === 'object') {
      const out = {}
      for (const key of Object.keys(value).sort()) out[key] = normalize(value[key])
      return out
    }
    return value
  }

  const check = (label, mine) => {
    const theirs = cpp.get(label)
    if (!theirs) {
      console.log(`! C++ 没有输出 ${label}`)
      return
    }
    checked += 1
    if (JSON.stringify(normalize(theirs)) !== JSON.stringify(normalize(mine))) {
      diffs.push({ label, cpp: theirs, js: mine })
    }
  }

  for (const name of files) {
    const text = readTextIfExists(join(projectRoot, name))
    if (!text) continue

    check(`load:${name}`, core.loadText(text, name))
    check(`snapshot:${name}`, core.snapshot())
    check(`save:${name}`, core.saveText())

    for (const mode of [0, 1]) {
      for (let start = 1; start <= 6; start += 1) {
        // 起点越界时只比较“两边都失败”，错误文案不要求一字不差
        const label = `traverse:${name}|${mode}:${start}`
        const theirs = cpp.get(label)
        const mine = core.traverse(mode, start)
        if (theirs && !theirs.ok) {
          checked += 1
          if (mine.ok) diffs.push({ label, cpp: theirs, js: mine })
        } else {
          check(label, mine)
        }
      }
    }

    for (const seq of sequences) check(`check:${name}|1:${seq}`, core.checkSequence(1, seq))

    for (let s = 1; s <= 4; s += 1) {
      for (let t = 1; t <= 6; t += 1) {
        if (s === t) continue
        check(`flow:${name}|${s}:${t}`, core.maxFlow(s, t))
      }
    }
  }

  console.log(`\n对比完成：${checked} 项，不一致 ${diffs.length} 项`)
  for (const d of diffs.slice(0, 6)) {
    console.log(`\n--- ${d.label}`)
    console.log('C++:', JSON.stringify(normalize(d.cpp)))
    console.log(' JS:', JSON.stringify(normalize(d.js)))
  }

  const randomized = core.random(8, 12, 1, 12345, 9)
  console.log(
    `随机图自检：${JSON.stringify(randomized)}（两边随机数发生器不同，只确认能建图）`,
  )

  process.exitCode = diffs.length === 0 ? 0 : 1
}

function readTextIfExists(path) {
  try {
    return readFileSync(path, 'utf8')
  } catch {
    console.log(`! 读不到 ${path}，这一组跳过`)
    return null
  }
}
