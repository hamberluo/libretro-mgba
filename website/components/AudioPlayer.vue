<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useData } from 'vitepress'

const props = defineProps({ src: { type: String, required: true } })

const { site } = useData()

const audio = ref(null)
const rootEl = ref(null)
const inView = ref(true)
let io = null
const started = ref(false)
const playing = ref(false)
const cur = ref(0)
const dur = ref(0)
const rates = [1, 1.25, 1.5, 0.75]
const rateIdx = ref(0)
const rate = computed(() => rates[rateIdx.value])

const resolvedSrc = computed(() => {
  const base = site.value.base || '/'          // 如 /libretro-mgba/
  const rel = props.src.replace(/^\//, '')      // 去掉 src 前导斜杠
  return base.replace(/\/$/, '') + '/' + rel    // 拼成 /libretro-mgba/audio/x.mp3
})

function fmt(t) {
  if (!t || isNaN(t)) return '0:00'
  const m = Math.floor(t / 60)
  const s = Math.floor(t % 60)
  return m + ':' + String(s).padStart(2, '0')
}
const progress = computed(() => dur.value ? (cur.value / dur.value) * 100 : 0)
const showMini = computed(() => started.value && !inView.value)

function toggle() {
  const a = audio.value
  if (!a) return
  if (a.paused) { a.play(); playing.value = true; started.value = true }
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
const RATE_KEY = 'gba-listen-rate'

function cycleRate() {
  rateIdx.value = (rateIdx.value + 1) % rates.length
  if (audio.value) audio.value.playbackRate = rate.value
  try { localStorage.setItem(RATE_KEY, String(rate.value)) } catch (e) {}
}

onMounted(() => {
  // 恢复全局倍速偏好（仅客户端，SSR 不读 localStorage）
  try {
    const saved = parseFloat(localStorage.getItem(RATE_KEY))
    const i = rates.indexOf(saved)
    if (i >= 0) rateIdx.value = i
  } catch (e) {}
  if (audio.value) audio.value.playbackRate = rate.value
  if (rootEl.value && typeof IntersectionObserver !== 'undefined') {
    io = new IntersectionObserver(([e]) => { inView.value = e.isIntersecting }, { threshold: 0 })
    io.observe(rootEl.value)
  }
})
onUnmounted(() => {
  if (audio.value) audio.value.pause()
  if (io) io.disconnect()
})
</script>

<template>
  <div class="ap" ref="rootEl">
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

  <Teleport to="body">
    <Transition name="mini-fade">
      <div class="ap-mini" v-if="showMini">
        <button class="play" @click="toggle" :aria-label="playing ? '暂停' : '播放'">
          <span v-if="!playing">▶</span><span v-else>❚❚</span>
        </button>
        <span class="mlabel">听书</span>
        <div class="bar" @click="seek"><div class="fill" :style="{ width: progress + '%' }"></div></div>
        <span class="time">{{ fmt(cur) }}</span>
      </div>
    </Transition>
  </Teleport>
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
.ap-mini {
  position: fixed; right: 1.2rem; bottom: 1.2rem; z-index: 100;
  display: flex; align-items: center; gap: 0.6rem;
  padding: 0.5rem 0.8rem; width: min(320px, 80vw);
  border: 1px solid #00d4aa; border-radius: 12px;
  background: var(--vp-c-bg-elv, var(--vp-c-bg-soft));
  box-shadow: 0 6px 20px rgba(0,0,0,0.35);
}
.ap-mini .play {
  width: 2rem; height: 2rem; font-size: 0.8rem; flex-shrink: 0;
  display: flex; align-items: center; justify-content: center;
  border: 1px solid #00d4aa; background: transparent; color: #00d4aa; border-radius: 8px; cursor: pointer;
}
.ap-mini .play:hover { background: rgba(0,212,170,0.12); }
.ap-mini .mlabel { flex-shrink: 0; font-size: 0.85rem; color: var(--vp-c-text-2); white-space: nowrap; }
.ap-mini .bar { flex: 1; min-width: 40px; height: 6px; background: var(--vp-c-bg); border-radius: 3px; cursor: pointer; overflow: hidden; }
.ap-mini .fill { height: 100%; background: #00d4aa; border-radius: 3px; }
.ap-mini .time { flex-shrink: 0; font-size: 0.78rem; color: var(--vp-c-text-3); font-family: var(--vp-font-family-mono); white-space: nowrap; }
.mini-fade-enter-active, .mini-fade-leave-active { transition: opacity 0.25s ease, transform 0.25s ease; }
.mini-fade-enter-from, .mini-fade-leave-to { opacity: 0; transform: translateY(10px); }
</style>
