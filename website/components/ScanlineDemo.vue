<script setup>
import { ref, computed, onUnmounted } from 'vue'

const ROWS = 16
const VISIBLE = 16

const vcount = ref(0)
const drawn = ref(0)
const playing = ref(false)
let timer = null

const inVblank = computed(() => drawn.value >= VISIBLE)
const phase = computed(() => {
  if (inVblank.value) return 'VBlank（一帧完成）'
  return vcount.value < VISIBLE ? 'HDraw / HBlank（正在画第 ' + vcount.value + ' 行）' : ''
})

function drawNext() {
  if (inVblank.value) return
  drawn.value += 1
  vcount.value = Math.min(drawn.value, VISIBLE)
}
function play() {
  if (playing.value || inVblank.value) return
  playing.value = true
  timer = setInterval(() => {
    drawNext()
    if (inVblank.value) pause()
  }, 180)
}
function pause() {
  playing.value = false
  if (timer) { clearInterval(timer); timer = null }
}
function reset() {
  pause()
  vcount.value = 0
  drawn.value = 0
}
onUnmounted(() => { if (timer) clearInterval(timer) })

const rows = computed(() => Array.from({ length: ROWS }, (_, i) => i))
function rowClass(i) {
  if (i < drawn.value) return 'done'
  if (i === drawn.value && !inVblank.value) return 'active'
  return 'pending'
}
</script>

<template>
  <div class="scan">
    <div class="screen" :class="{ vblank: inVblank }">
      <div
        v-for="i in rows"
        :key="i"
        class="row"
        :class="rowClass(i)"
      ></div>
    </div>
    <div class="info">
      <span>vcount <b>{{ Math.min(vcount, VISIBLE) }}</b> / {{ VISIBLE }}</span>
      <span>阶段 <b>{{ phase }}</b></span>
    </div>
    <p v-if="inVblank" class="vbmsg">✓ 一帧完成！进入 VBlank——DMA 可以趁现在搬下一帧数据了。</p>
    <div class="controls">
      <button @click="drawNext" :disabled="inVblank">画下一行</button>
      <button v-if="!playing" @click="play" :disabled="inVblank">自动播放</button>
      <button v-else @click="pause">暂停</button>
      <button @click="reset">重置</button>
    </div>
    <p class="hint">教学示意。一帧 = 一行行画下来（真实 GBA 是 160 行），画完进入 VBlank。这就是序章那条从上往下的扫描线。</p>
  </div>
</template>

<style scoped>
.scan { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.screen {
  display: flex; flex-direction: column; gap: 2px;
  width: 240px; max-width: 100%; margin: 0 auto;
  padding: 6px; border: 2px solid var(--vp-c-divider); border-radius: 8px;
  background: #050a12; transition: box-shadow 0.3s ease;
}
.screen.vblank { box-shadow: 0 0 0 3px rgba(255, 209, 102, 0.4); }
.row { height: 10px; border-radius: 2px; background: #0d1b2a; transition: all 0.18s ease; }
.row.done { background: linear-gradient(90deg, #00d4aa, #5b8def); }
.row.active { background: #ffd166; box-shadow: 0 0 6px rgba(255, 209, 102, 0.8); }
.row.pending { background: #0d1b2a; }
.info { display: flex; gap: 1.5rem; justify-content: center; margin-top: 1rem; font-size: 0.85rem; color: var(--vp-c-text-2); }
.info b { color: #00d4aa; font-family: var(--vp-font-family-mono); white-space: nowrap; }
.vbmsg { text-align: center; margin-top: 0.6rem; font-size: 0.85rem; color: #ffd166; }
.controls { display: flex; gap: 0.6rem; justify-content: center; margin-top: 1rem; flex-wrap: wrap; }
.controls button {
  padding: 0.4rem 1rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.9rem; transition: background 0.2s;
}
.controls button:hover { background: rgba(0, 212, 170, 0.12); }
.controls button:disabled { opacity: 0.4; cursor: not-allowed; }
.hint { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-3); text-align: center; }
</style>
