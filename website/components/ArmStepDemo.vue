<script setup>
import { ref, reactive } from 'vue'

const codeLines = [
  'opcode = prefetch[0];          // 取出当前指令',
  'prefetch[0] = prefetch[1];     // 下一条递补',
  'PC += 4;                       // 指向再下一条',
  'prefetch[1] = LOAD(PC);        // 预读填满队列',
  'if (!conditionMet) { cycles++; return; }  // 条件不满足则跳过',
  'instr = armTable[index(opcode)];  // 查表解码',
  'instr(cpu, opcode);            // 执行',
]

const program = ['0xE3A00001', '0xE2800002', '0xE1A01000']

const activeLine = ref(-1)
const stepCount = ref(0)
const state = reactive({
  pc: '0x8000000',
  p0: program[0],
  p1: program[1],
  cycles: 0,
  changed: {},
})
let nextInstr = 2

function markChanged(...keys) {
  state.changed = {}
  for (const k of keys) state.changed[k] = true
}

function step() {
  const line = (activeLine.value + 1) % codeLines.length
  activeLine.value = line
  if (line === 0) {
    markChanged()
  } else if (line === 1) {
    state.p0 = state.p1
    markChanged('p0')
  } else if (line === 2) {
    state.pc = '0x' + (parseInt(state.pc, 16) + 4).toString(16).toUpperCase()
    markChanged('pc')
  } else if (line === 3) {
    state.p1 = program[nextInstr % program.length]
    nextInstr++
    markChanged('p1')
  } else if (line === 4) {
    markChanged()
  } else if (line === 5) {
    markChanged()
  } else if (line === 6) {
    state.cycles += 1
    markChanged('cycles')
    stepCount.value++
  }
}

function reset() {
  activeLine.value = -1
  stepCount.value = 0
  nextInstr = 2
  state.pc = '0x8000000'
  state.p0 = program[0]
  state.p1 = program[1]
  state.cycles = 0
  state.changed = {}
}
</script>

<template>
  <div class="armstep">
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
        <div class="srow" :class="{ changed: state.changed.pc }">
          <span class="k">PC</span><span class="v">{{ state.pc }}</span>
        </div>
        <div class="srow" :class="{ changed: state.changed.p0 }">
          <span class="k">prefetch[0]</span><span class="v">{{ state.p0 }}</span>
        </div>
        <div class="srow" :class="{ changed: state.changed.p1 }">
          <span class="k">prefetch[1]</span><span class="v">{{ state.p1 }}</span>
        </div>
        <div class="srow" :class="{ changed: state.changed.cycles }">
          <span class="k">cycles</span><span class="v">{{ state.cycles }}</span>
        </div>
      </div>
    </div>
    <div class="controls">
      <button @click="step">单步</button>
      <button @click="reset">重置</button>
      <span class="meta">已执行指令：{{ stepCount }}</span>
    </div>
    <p class="hint">教学示意，非精确 ARM 仿真。注意 PC 始终领先正在执行的指令——这就是流水线预取。</p>
  </div>
</template>

<style scoped>
.armstep {
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  padding: 1.2rem;
  margin: 1.5rem 0;
  background: var(--vp-c-bg-soft);
}
.cols { display: flex; gap: 1rem; align-items: flex-start; }
.code { flex: 3; min-width: 0; font-family: var(--vp-font-family-mono); font-size: 0.8rem; overflow-x: auto; }
.line {
  padding: 0.25rem 0.5rem;
  border-radius: 6px;
  white-space: pre;
  color: var(--vp-c-text-2);
  transition: all 0.25s ease;
}
.line.active {
  background: rgba(0, 212, 170, 0.16);
  color: var(--vp-c-text-1);
  box-shadow: inset 3px 0 0 #00d4aa;
}
.state {
  flex: 2;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  font-family: var(--vp-font-family-mono);
  font-size: 0.8rem;
}
.srow {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  gap: 0.5rem;
  padding: 0.4rem 0.6rem;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  transition: all 0.3s ease;
}
.srow.changed {
  border-color: #00d4aa;
  background: rgba(0, 212, 170, 0.12);
}
.srow .k { color: var(--vp-c-text-3); flex-shrink: 0; }
.srow .v { color: #00d4aa; font-weight: 600; white-space: nowrap; }
.controls {
  display: flex;
  align-items: center;
  gap: 0.6rem;
  margin-top: 1rem;
}
.controls button {
  padding: 0.4rem 1rem;
  border-radius: 8px;
  border: 1px solid #00d4aa;
  background: transparent;
  color: #00d4aa;
  cursor: pointer;
  font-size: 0.9rem;
  transition: background 0.2s;
}
.controls button:hover { background: rgba(0, 212, 170, 0.12); }
.controls .meta { font-size: 0.8rem; color: var(--vp-c-text-3); }
.hint { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
@media (max-width: 640px) {
  .cols { flex-direction: column; }
}
</style>
