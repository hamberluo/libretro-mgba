<script setup>
import { ref, reactive } from 'vue'

const presets = [
  { name: '正常加', rn: 0x00000003, rm: 0x00000004 },
  { name: '触发进位 C', rn: 0xFFFFFFFF, rm: 0x00000002 },
  { name: '触发溢出 V', rn: 0x7FFFFFFF, rm: 0x00000001 },
]

const codeLines = [
  'rd = opcode & 0x7;            // 解码目标寄存器号',
  'rn = (opcode >> 3) & 0x7;     // 解码操作数 1',
  'rm = (opcode >> 6) & 0x7;     // 解码操作数 2',
  'D = gprs[rn] + gprs[rm];      // 相加',
  'cpsr.n = D >> 31;             // 负标志',
  'cpsr.z = (D == 0);            // 零标志',
  'cpsr.c = carryFrom(rn,rm,D);  // 进位标志',
  'cpsr.v = overflow(rn,rm,D);   // 溢出标志',
]

const sel = ref(0)
const activeLine = ref(-1)
const state = reactive({
  rn: '0x00000003',
  rm: '0x00000004',
  rd: '—',
  n: '—', z: '—', c: '—', v: '—',
  changed: {},
})

function hex(v) {
  return '0x' + (v >>> 0).toString(16).toUpperCase().padStart(8, '0')
}
function markChanged(...keys) {
  state.changed = {}
  for (const k of keys) state.changed[k] = true
}
function loadPreset(i) {
  sel.value = i
  activeLine.value = -1
  const p = presets[i]
  state.rn = hex(p.rn)
  state.rm = hex(p.rm)
  state.rd = '—'
  state.n = state.z = state.c = state.v = '—'
  state.changed = {}
}

function step() {
  const line = (activeLine.value + 1) % codeLines.length
  activeLine.value = line
  const p = presets[sel.value]
  const m = p.rn >>> 0
  const n = p.rm >>> 0
  const d = (m + n) >>> 0
  if (line <= 2) {
    markChanged()
  } else if (line === 3) {
    state.rd = hex(d)
    markChanged('rd')
  } else if (line === 4) {
    state.n = (d >>> 31) & 1
    markChanged('n')
  } else if (line === 5) {
    state.z = d === 0 ? 1 : 0
    markChanged('z')
  } else if (line === 6) {
    state.c = ((m >>> 31) + (n >>> 31)) > (d >>> 31) ? 1 : 0
    markChanged('c')
  } else if (line === 7) {
    const sameSign = ((m ^ n) >>> 31) === 0
    const flipped = ((m ^ d) >>> 31) === 1
    state.v = (sameSign && flipped) ? 1 : 0
    markChanged('v')
  }
}
</script>

<template>
  <div class="tadd">
    <div class="presets">
      <button
        v-for="(p, i) in presets"
        :key="i"
        :class="{ on: i === sel }"
        @click="loadPreset(i)"
      >{{ p.name }}</button>
    </div>
    <div class="cols">
      <div class="code">
        <div
          v-for="(ln, i) in codeLines"
          :key="i"
          class="line"
          :class="{ active: i === activeLine }"
        >{{ ln }}</div>
      </div>
      <div class="state">
        <div class="srow" :class="{ changed: state.changed.rn }"><span class="k">rn</span><span class="v">{{ state.rn }}</span></div>
        <div class="srow" :class="{ changed: state.changed.rm }"><span class="k">rm</span><span class="v">{{ state.rm }}</span></div>
        <div class="srow" :class="{ changed: state.changed.rd }"><span class="k">结果 rd</span><span class="v">{{ state.rd }}</span></div>
        <div class="flags">
          <div class="flag" :class="{ changed: state.changed.n }"><span>N</span><b>{{ state.n }}</b></div>
          <div class="flag" :class="{ changed: state.changed.z }"><span>Z</span><b>{{ state.z }}</b></div>
          <div class="flag" :class="{ changed: state.changed.c }"><span>C</span><b>{{ state.c }}</b></div>
          <div class="flag" :class="{ changed: state.changed.v }"><span>V</span><b>{{ state.v }}</b></div>
        </div>
      </div>
    </div>
    <div class="controls">
      <button @click="step">单步</button>
      <button @click="loadPreset(sel)">重置</button>
    </div>
    <p class="hint">教学示意，非精确 ARM 仿真。换不同预设，看同一条 ADD 如何设出不同的 N/Z/C/V。</p>
  </div>
</template>

<style scoped>
.tadd { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.presets { display: flex; gap: 0.5rem; margin-bottom: 1rem; flex-wrap: wrap; }
.presets button, .controls button {
  padding: 0.35rem 0.9rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.85rem; transition: background 0.2s;
}
.presets button.on { background: #00d4aa; color: #0d1b2a; font-weight: 700; }
.presets button:hover, .controls button:hover { background: rgba(0, 212, 170, 0.12); }
.cols { display: flex; gap: 1rem; }
.code { flex: 2; font-family: var(--vp-font-family-mono); font-size: 0.78rem; }
.line { padding: 0.22rem 0.5rem; border-radius: 6px; white-space: pre; color: var(--vp-c-text-2); transition: all 0.25s ease; }
.line.active { background: rgba(0, 212, 170, 0.16); color: var(--vp-c-text-1); box-shadow: inset 3px 0 0 #00d4aa; }
.state { flex: 1; display: flex; flex-direction: column; gap: 0.5rem; font-family: var(--vp-font-family-mono); font-size: 0.85rem; }
.srow { display: flex; justify-content: space-between; padding: 0.4rem 0.6rem; border: 1px solid var(--vp-c-divider); border-radius: 8px; transition: all 0.3s ease; }
.srow.changed { border-color: #00d4aa; background: rgba(0, 212, 170, 0.12); }
.srow .k { color: var(--vp-c-text-3); }
.srow .v { color: #00d4aa; font-weight: 600; }
.flags { display: flex; gap: 0.5rem; }
.flag { flex: 1; display: flex; flex-direction: column; align-items: center; gap: 0.2rem; padding: 0.4rem; border: 1px solid var(--vp-c-divider); border-radius: 8px; transition: all 0.3s ease; }
.flag.changed { border-color: #00d4aa; background: rgba(0, 212, 170, 0.12); }
.flag span { color: var(--vp-c-text-3); font-size: 0.75rem; }
.flag b { color: #00d4aa; font-size: 1.1rem; }
.controls { display: flex; gap: 0.6rem; margin-top: 1rem; }
.hint { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
@media (max-width: 640px) { .cols { flex-direction: column; } }
</style>
