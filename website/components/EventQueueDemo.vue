<script setup>
import { ref, reactive, computed } from 'vue'

const defs = [
  { name: 'PPU 画一行', period: 240, color: '#00d4aa' },
  { name: '定时器溢出', period: 350, color: '#ffd166' },
  { name: 'DMA 传输',   period: 600, color: '#5b8def' },
]

const TIMELINE = 1200

const now = ref(0)
const log = ref('点「推进」让 CPU 跳到下一个事件点。')
const queue = reactive([])
let uid = 0

function seed() {
  queue.length = 0
  now.value = 0
  uid = 0
  defs.forEach(d => queue.push({ id: ++uid, name: d.name, period: d.period, when: d.period, color: d.color }))
  sortQ()
  log.value = '点「推进」让 CPU 跳到下一个事件点。'
}
function sortQ() { queue.sort((a, b) => a.when - b.when || a.id - b.id) }

const nextWhen = computed(() => queue.length ? queue[0].when : null)

function advance() {
  if (!queue.length) return
  const ev = queue[0]
  now.value = ev.when
  queue.shift()
  const nextTime = ev.when + ev.period
  if (nextTime <= TIMELINE * 3) {
    queue.push({ id: ++uid, name: ev.name, period: ev.period, when: nextTime, color: ev.color })
  }
  sortQ()
  log.value = `周期 ${ev.when}：触发「${ev.name}」→ 它登记了下一次（周期 ${nextTime}）。CPU 不空转，直接跳到这里。`
}

function posOf(when) { return Math.min(100, (when / TIMELINE) * 100) }
const cursorPos = computed(() => posOf(now.value))

seed()
</script>

<template>
  <div class="eq">
    <div class="timeline">
      <div class="track">
        <div class="cursor" :style="{ left: cursorPos + '%' }"><span>{{ now }}</span></div>
        <div
          v-for="ev in queue"
          :key="ev.id"
          class="marker"
          :style="{ left: posOf(ev.when) + '%', borderColor: ev.color }"
          :title="ev.name + ' @ ' + ev.when"
        ></div>
      </div>
      <div class="axis-label">时间轴（周期）→</div>
    </div>
    <div class="queue">
      <div class="qhead">事件队列（按触发时刻排序）</div>
      <div
        v-for="(ev, i) in queue"
        :key="ev.id"
        class="qrow"
        :class="{ next: i === 0 }"
        :style="{ borderLeftColor: ev.color }"
      >
        <span class="qname">{{ ev.name }}</span>
        <span class="qwhen">周期 {{ ev.when }}</span>
      </div>
    </div>
    <div class="controls">
      <button @click="advance">推进到下一个事件</button>
      <button @click="seed">重置</button>
      <span class="meta" v-if="nextWhen !== null">下一个事件 @ 周期 {{ nextWhen }}</span>
    </div>
    <p class="log">{{ log }}</p>
    <p class="hint">教学示意。注意游标是「跳」到下一个事件点，不是一格格爬——这就是事件驱动：用「下一件事在何时」代替逐周期轮询。</p>
  </div>
</template>

<style scoped>
.eq { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.timeline { margin-bottom: 1rem; }
.track {
  position: relative; height: 48px; border: 1px solid var(--vp-c-divider);
  border-radius: 8px; background: var(--vp-c-bg);
}
.cursor {
  position: absolute; top: 0; bottom: 0; width: 2px; background: #ff6b6b;
  transition: left 0.4s ease;
}
.cursor span {
  position: absolute; top: -1.3rem; left: 50%; transform: translateX(-50%);
  font-size: 0.7rem; color: #ff6b6b; font-family: var(--vp-font-family-mono); white-space: nowrap;
}
.marker {
  position: absolute; top: 50%; transform: translate(-50%, -50%);
  width: 12px; height: 12px; border-radius: 50%; border: 3px solid; background: var(--vp-c-bg-soft);
  transition: left 0.4s ease;
}
.axis-label { font-size: 0.75rem; color: var(--vp-c-text-3); text-align: right; margin-top: 0.3rem; }
.queue { margin-bottom: 1rem; }
.qhead { font-size: 0.85rem; color: var(--vp-c-text-3); margin-bottom: 0.5rem; }
.qrow {
  display: flex; justify-content: space-between; align-items: baseline; gap: 0.5rem;
  padding: 0.4rem 0.7rem; margin-bottom: 0.3rem;
  border: 1px solid var(--vp-c-divider); border-left: 3px solid; border-radius: 6px;
  transition: all 0.25s ease;
}
.qrow.next { background: rgba(0, 212, 170, 0.14); border-color: #00d4aa; }
.qname { color: var(--vp-c-text-1); font-size: 0.9rem; }
.qwhen { color: var(--vp-c-text-3); font-family: var(--vp-font-family-mono); font-size: 0.82rem; white-space: nowrap; }
.controls { display: flex; align-items: center; gap: 0.6rem; flex-wrap: wrap; }
.controls button {
  padding: 0.4rem 1rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.9rem; transition: background 0.2s;
}
.controls button:hover { background: rgba(0, 212, 170, 0.12); }
.controls .meta { font-size: 0.8rem; color: var(--vp-c-text-3); font-family: var(--vp-font-family-mono); white-space: nowrap; }
.log { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-2); min-height: 1.2em; }
.hint { margin-top: 0.4rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
</style>
