<script setup>
import { ref, reactive, computed } from 'vue'

const SRC = ['0x1111', '0x2222', '0x3333', '0x4444']
const COUNT = SRC.length
const SRC_BASE = 0x02000000
const DST_BASE = 0x06000000
const WIDTH = 4

const idx = ref(-1)
const cycles = ref(0)
const dst = reactive(['—', '—', '—', '—'])

const done = computed(() => idx.value >= COUNT - 1)
const cpuBlocked = computed(() => idx.value >= 0 && !done.value)
const srcPtr = computed(() => '0x' + (SRC_BASE + Math.max(0, idx.value + 1) * WIDTH).toString(16).toUpperCase())
const dstPtr = computed(() => '0x' + (DST_BASE + Math.max(0, idx.value + 1) * WIDTH).toString(16).toUpperCase())

const cpuState = computed(() => {
  if (idx.value < 0) return '▶ 运行中'
  if (done.value) return '▶ 运行中（总线已交还）'
  return '⛔ 已阻塞（总线被 DMA 占用）'
})

function step() {
  if (done.value) return
  const i = idx.value + 1
  dst[i] = SRC[i]
  cycles.value += 2
  idx.value = i
}
function reset() {
  idx.value = -1
  cycles.value = 0
  for (let i = 0; i < COUNT; i++) dst[i] = '—'
}
</script>

<template>
  <div class="dma">
    <div class="lanes">
      <div class="lane">
        <div class="lhead">源 source</div>
        <div
          v-for="(v, i) in SRC"
          :key="i"
          class="cell"
          :class="{ active: i === idx, moved: i < idx }"
        >{{ v }}</div>
      </div>
      <div class="cpu">
        <div class="lhead">CPU</div>
        <div class="cpu-state" :class="{ blocked: cpuBlocked }">{{ cpuState }}</div>
        <div class="arrow">→ DMA 搬运 →</div>
      </div>
      <div class="lane">
        <div class="lhead">目标 dest</div>
        <div
          v-for="(v, i) in dst"
          :key="i"
          class="cell"
          :class="{ active: i === idx }"
        >{{ v }}</div>
      </div>
    </div>
    <div class="info">
      <span>下一源地址 <b>{{ srcPtr }}</b></span>
      <span>下一目标 <b>{{ dstPtr }}</b></span>
      <span>已用周期 <b>{{ cycles }}</b></span>
    </div>
    <div class="controls">
      <button @click="step" :disabled="done">单步搬运</button>
      <button @click="reset">重置</button>
    </div>
    <p class="hint">教学示意。注意搬运期间 CPU 是「已阻塞」——DMA 直接占用总线，搬完才交还。它不是和 CPU 并行，而是短暂霸占。</p>
  </div>
</template>

<style scoped>
.dma { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.lanes { display: flex; gap: 1rem; align-items: flex-start; }
.lane { flex: 1; min-width: 0; }
.cpu { flex: 1.2; min-width: 0; text-align: center; }
.lhead { font-size: 0.8rem; color: var(--vp-c-text-3); margin-bottom: 0.5rem; }
.cell {
  font-family: var(--vp-font-family-mono); font-size: 0.85rem;
  padding: 0.45rem 0.6rem; margin-bottom: 0.35rem;
  border: 1px solid var(--vp-c-divider); border-radius: 6px;
  color: var(--vp-c-text-2); white-space: nowrap; transition: all 0.25s ease;
}
.cell.moved { opacity: 0.5; }
.cell.active { border-color: #00d4aa; background: rgba(0, 212, 170, 0.14); color: var(--vp-c-text-1); }
.cpu-state {
  padding: 0.5rem; border: 1px solid var(--vp-c-divider); border-radius: 8px;
  font-size: 0.85rem; color: var(--vp-c-text-2); margin-bottom: 0.6rem; white-space: nowrap;
}
.cpu-state.blocked { border-color: #ff6b6b; color: #ff6b6b; background: rgba(255, 107, 107, 0.1); }
.arrow { font-size: 0.8rem; color: #00d4aa; }
.info { display: flex; gap: 1.2rem; flex-wrap: wrap; margin-top: 1rem; font-size: 0.85rem; color: var(--vp-c-text-2); }
.info b { color: #00d4aa; font-family: var(--vp-font-family-mono); white-space: nowrap; }
.controls { display: flex; gap: 0.6rem; margin-top: 1rem; }
.controls button {
  padding: 0.4rem 1rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.9rem; transition: background 0.2s;
}
.controls button:hover { background: rgba(0, 212, 170, 0.12); }
.controls button:disabled { opacity: 0.4; cursor: not-allowed; }
.hint { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
@media (max-width: 640px) { .lanes { flex-direction: column; } }
</style>
