<script setup>
import { ref, reactive, computed } from 'vue'

const presets = [
  { label: '10 ÷ 3', r0: 10, r1: 3 },
  { label: '100 ÷ 7', r0: 100, r1: 7 },
  { label: '20 ÷ 4', r0: 20, r1: 4 },
]

const steps = [
  '游戏执行  swi 0x06        // 请求「除法」服务',
  'mGBA 拦截 SWI，检查 fullBios',
  '无真 BIOS → 走 HLE',
  '调 C 函数 _Div(r0, r1)    // 一个函数直接算',
  '商写回 r0，余数写回 r1',
]

const sel = ref(0)
const active = ref(-1)
const state = reactive({ r0: 10, r1: 3, quot: '—', rem: '—', changed: {} })

function mark(...k) { state.changed = {}; for (const x of k) state.changed[x] = true }
function load(i) {
  sel.value = i; active.value = -1
  state.r0 = presets[i].r0; state.r1 = presets[i].r1
  state.quot = '—'; state.rem = '—'; state.changed = {}
}
function step() {
  const n = (active.value + 1) % steps.length
  active.value = n
  const p = presets[sel.value]
  if (n === 4) {
    state.quot = Math.trunc(p.r0 / p.r1)
    state.rem = p.r0 % p.r1
    mark('quot', 'rem')
  } else {
    mark()
  }
}
</script>

<template>
  <div class="swi">
    <div class="presets">
      <button v-for="(p, i) in presets" :key="i" :class="{ on: i === sel }" @click="load(i)">{{ p.label }}</button>
    </div>
    <div class="cols">
      <div class="steps">
        <div v-for="(s, i) in steps" :key="i" class="line" :class="{ active: i === active }">{{ s }}</div>
      </div>
      <div class="state">
        <div class="srow" :class="{ changed: state.changed.r0 }"><span class="k">r0 被除数</span><span class="v">{{ state.r0 }}</span></div>
        <div class="srow" :class="{ changed: state.changed.r1 }"><span class="k">r1 除数</span><span class="v">{{ state.r1 }}</span></div>
        <div class="srow" :class="{ changed: state.changed.quot }"><span class="k">r0 ← 商</span><span class="v">{{ state.quot }}</span></div>
        <div class="srow" :class="{ changed: state.changed.rem }"><span class="k">r1 ← 余</span><span class="v">{{ state.rem }}</span></div>
      </div>
    </div>
    <div class="controls">
      <button @click="step">单步</button>
      <button @click="load(sel)">重置</button>
    </div>
    <p class="cmp">HLE：1 个 C 函数算完　|　LLE（真 BIOS）：要逐条跑几十条 ARM 指令</p>
    <p class="hint">教学示意。游戏用 SWI 编号请求服务，HLE 拦截后用宿主代码直接给结果——不需要真 BIOS ROM。</p>
  </div>
</template>

<style scoped>
.swi { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.presets { display: flex; gap: 0.5rem; margin-bottom: 1rem; flex-wrap: wrap; }
.presets button, .controls button {
  padding: 0.35rem 0.9rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.85rem; transition: background 0.2s;
}
.presets button.on { background: #00d4aa; color: #0d1b2a; font-weight: 700; }
.presets button:hover, .controls button:hover { background: rgba(0, 212, 170, 0.12); }
.presets button.on:hover { background: #4af0d2; color: #0d1b2a; }
.cols { display: flex; gap: 1rem; }
.steps { flex: 3; min-width: 0; font-family: var(--vp-font-family-mono); font-size: 0.78rem; }
.line { padding: 0.3rem 0.5rem; border-radius: 6px; white-space: pre; color: var(--vp-c-text-2); transition: all 0.25s ease; }
.line.active { background: rgba(0, 212, 170, 0.14); color: var(--vp-c-text-1); box-shadow: inset 3px 0 0 #00d4aa; }
.state { flex: 2; min-width: 0; display: flex; flex-direction: column; gap: 0.5rem; font-family: var(--vp-font-family-mono); font-size: 0.82rem; }
.srow { display: flex; justify-content: space-between; align-items: baseline; gap: 0.5rem; padding: 0.4rem 0.6rem; border: 1px solid var(--vp-c-divider); border-radius: 8px; transition: all 0.3s ease; }
.srow.changed { border-color: #00d4aa; background: rgba(0, 212, 170, 0.14); }
.srow .k { color: var(--vp-c-text-3); flex-shrink: 0; }
.srow .v { color: #00d4aa; font-weight: 600; white-space: nowrap; }
.controls { display: flex; gap: 0.6rem; margin-top: 1rem; }
.cmp { margin-top: 0.9rem; font-size: 0.82rem; color: #ffd166; }
.hint { margin-top: 0.4rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
@media (max-width: 640px) { .cols { flex-direction: column; } }
</style>
