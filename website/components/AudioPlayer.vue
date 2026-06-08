<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { withBase } from 'vitepress'

const props = defineProps({ src: { type: String, required: true } })

const audio = ref(null)
const playing = ref(false)
const cur = ref(0)
const dur = ref(0)
const rates = [1, 1.25, 1.5, 0.75]
const rateIdx = ref(0)
const rate = computed(() => rates[rateIdx.value])

const resolvedSrc = computed(() => withBase(props.src))

function fmt(t) {
  if (!t || isNaN(t)) return '0:00'
  const m = Math.floor(t / 60)
  const s = Math.floor(t % 60)
  return m + ':' + String(s).padStart(2, '0')
}
const progress = computed(() => dur.value ? (cur.value / dur.value) * 100 : 0)

function toggle() {
  const a = audio.value
  if (!a) return
  if (a.paused) { a.play(); playing.value = true }
  else { a.pause(); playing.value = false }
}
function onTime() { cur.value = audio.value.currentTime }
function onMeta() { dur.value = audio.value.duration }
function onEnd() { playing.value = false }
function seek(e) {
  const a = audio.value
  if (!a || !dur.value) return
  const rect = e.currentTarget.getBoundingClientRect()
  const ratio = (e.clientX - rect.left) / rect.width
  a.currentTime = ratio * dur.value
}
function cycleRate() {
  rateIdx.value = (rateIdx.value + 1) % rates.length
  if (audio.value) audio.value.playbackRate = rate.value
}

onMounted(() => { if (audio.value) audio.value.playbackRate = rate.value })
onUnmounted(() => { if (audio.value) audio.value.pause() })
</script>

<template>
  <div class="ap">
    <audio
      ref="audio"
      :src="resolvedSrc"
      preload="metadata"
      @timeupdate="onTime"
      @loadedmetadata="onMeta"
      @ended="onEnd"
    ></audio>
    <button class="play" @click="toggle" :aria-label="playing ? '暂停' : '播放'">
      <span v-if="!playing">▶</span><span v-else>❚❚</span>
    </button>
    <span class="label">听书</span>
    <div class="bar" @click="seek">
      <div class="fill" :style="{ width: progress + '%' }"></div>
    </div>
    <span class="time">{{ fmt(cur) }} / {{ fmt(dur) }}</span>
    <button class="rate" @click="cycleRate">{{ rate }}×</button>
  </div>
</template>

<style scoped>
.ap {
  display: flex; align-items: center; gap: 0.7rem;
  padding: 0.6rem 0.9rem; margin: 1rem 0 1.5rem;
  border: 1px solid var(--vp-c-divider); border-radius: 10px;
  background: var(--vp-c-bg-soft);
}
.play, .rate {
  flex-shrink: 0; border: 1px solid #00d4aa; background: transparent; color: #00d4aa;
  border-radius: 8px; cursor: pointer; transition: background 0.2s;
}
.play { width: 2rem; height: 2rem; font-size: 0.8rem; display: flex; align-items: center; justify-content: center; }
.rate { padding: 0.25rem 0.6rem; font-size: 0.8rem; font-family: var(--vp-font-family-mono); white-space: nowrap; }
.play:hover, .rate:hover { background: rgba(0, 212, 170, 0.12); }
.label { flex-shrink: 0; font-size: 0.85rem; color: var(--vp-c-text-2); white-space: nowrap; }
.bar { flex: 1; min-width: 60px; height: 6px; background: var(--vp-c-bg); border-radius: 3px; cursor: pointer; overflow: hidden; }
.fill { height: 100%; background: #00d4aa; border-radius: 3px; transition: width 0.1s linear; }
.time { flex-shrink: 0; font-size: 0.78rem; color: var(--vp-c-text-3); font-family: var(--vp-font-family-mono); white-space: nowrap; }
@media (max-width: 640px) {
  .label { display: none; }
}
</style>
