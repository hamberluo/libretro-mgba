<script setup>
import { ref, reactive, computed } from 'vue'

const live = reactive({ pc: 0x8000000, r0: 0, cycles: 0, vcount: 0, samples: 0 })
const snapshot = ref(null)
const restoredFlash = ref(false)

function run() {
  live.pc += 4
  live.r0 = (live.r0 + 7) & 0xFF
  live.cycles += 280
  live.vcount = (live.vcount + 1) % 160
  live.samples += 32
}
function save() {
  snapshot.value = { ...live, magic: '0x01000004' }
}
function load() {
  if (!snapshot.value) return
  const s = snapshot.value
  live.pc = s.pc; live.r0 = s.r0; live.cycles = s.cycles
  live.vcount = s.vcount; live.samples = s.samples
  restoredFlash.value = true
  setTimeout(() => { restoredFlash.value = false }, 600)
}
function reset() {
  live.pc = 0x8000000; live.r0 = 0; live.cycles = 0; live.vcount = 0; live.samples = 0
  snapshot.value = null
}

const hx = (v) => '0x' + (v >>> 0).toString(16).toUpperCase()
const rows = computed(() => [
  { k: 'PC（程序计数器）', v: hx(live.pc), s: snapshot.value ? hx(snapshot.value.pc) : '—', ep: '第2集' },
  { k: 'r0（寄存器）', v: live.r0, s: snapshot.value ? snapshot.value.r0 : '—', ep: '第2集' },
  { k: 'masterCycles（时钟）', v: live.cycles, s: snapshot.value ? snapshot.value.cycles : '—', ep: '第5集' },
  { k: 'vcount（PPU 行）', v: live.vcount, s: snapshot.value ? snapshot.value.vcount : '—', ep: '第7集' },
  { k: 'audio samples', v: live.samples, s: snapshot.value ? snapshot.value.samples : '—', ep: '第9集' },
])
</script>

<template>
  <div class="ss">
    <div class="cols">
      <div class="machine" :class="{ flash: restoredFlash }">
        <div class="mhead">运行中的机器</div>
        <div v-for="(r, i) in rows" :key="i" class="srow">
          <span class="k">{{ r.k }} <em>{{ r.ep }}</em></span>
          <span class="v">{{ r.v }}</span>
        </div>
      </div>
      <div class="snap">
        <div class="mhead">存档快照</div>
        <template v-if="snapshot">
          <div class="magic">magic {{ snapshot.magic }}</div>
          <div v-for="(r, i) in rows" :key="i" class="srow">
            <span class="k">{{ r.k.split('（')[0] }}</span>
            <span class="v gold">{{ r.s }}</span>
          </div>
        </template>
        <div v-else class="empty">尚无存档，点「存档」拍下快照</div>
      </div>
    </div>
    <div class="controls">
      <button @click="run">运行一下</button>
      <button @click="save">存档</button>
      <button @click="load" :disabled="!snapshot">读档</button>
      <button @click="reset">重置</button>
    </div>
    <p class="hint">教学示意。存档 = 把每个部件的状态变量抄进快照；读档 = 把快照值精确抄回去。整台机器没有藏在硬件里的状态，全是变量——所以能被一次性冻结、还原。真机做不到。</p>
  </div>
</template>

<style scoped>
.ss { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.cols { display: flex; gap: 1rem; }
.machine, .snap { flex: 1; min-width: 0; border: 1px solid var(--vp-c-divider); border-radius: 8px; padding: 0.8rem; transition: all 0.3s ease; }
.machine.flash { border-color: #00d4aa; background: rgba(0, 212, 170, 0.14); }
.mhead { font-size: 0.82rem; color: var(--vp-c-text-3); margin-bottom: 0.6rem; }
.srow { display: flex; justify-content: space-between; align-items: baseline; gap: 0.5rem; padding: 0.3rem 0; font-size: 0.82rem; }
.srow .k { color: var(--vp-c-text-2); }
.srow .k em { color: var(--vp-c-text-3); font-style: normal; font-size: 0.7rem; }
.srow .v { color: #00d4aa; font-family: var(--vp-font-family-mono); white-space: nowrap; }
.srow .v.gold { color: #ffd166; }
.magic { font-size: 0.75rem; color: #ffd166; font-family: var(--vp-font-family-mono); margin-bottom: 0.4rem; white-space: nowrap; }
.empty { font-size: 0.82rem; color: var(--vp-c-text-3); padding: 1rem 0; text-align: center; }
.controls { display: flex; gap: 0.6rem; margin-top: 1rem; flex-wrap: wrap; }
.controls button {
  padding: 0.4rem 1rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.9rem; transition: background 0.2s;
}
.controls button:hover { background: rgba(0, 212, 170, 0.12); }
.controls button:disabled { opacity: 0.4; cursor: not-allowed; }
.hint { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
@media (max-width: 640px) { .cols { flex-direction: column; } }
</style>
