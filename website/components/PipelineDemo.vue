<script setup>
import { ref, onUnmounted } from 'vue'

const stages = [
  { key: 'fetch', name: '取指 Fetch', desc: '从内存取出下一条指令（PC 指向的地址）' },
  { key: 'decode', name: '解码 Decode', desc: '解析这条指令要 CPU 做什么（mGBA 用查表法）' },
  { key: 'execute', name: '执行 Execute', desc: '真正执行：读写寄存器/内存、跳转、运算' },
]

const active = ref(-1)
const playing = ref(false)
let timer = null

function step() {
  active.value = (active.value + 1) % stages.length
}

function play() {
  if (playing.value) return
  playing.value = true
  if (active.value < 0) active.value = 0
  timer = setInterval(() => {
    active.value = (active.value + 1) % stages.length
  }, 1200)
}

function pause() {
  playing.value = false
  if (timer) { clearInterval(timer); timer = null }
}

function reset() {
  pause()
  active.value = -1
}

onUnmounted(() => { if (timer) clearInterval(timer) })
</script>

<template>
  <div class="pipeline">
    <div class="stages">
      <div
        v-for="(s, i) in stages"
        :key="s.key"
        class="stage"
        :class="{ active: i === active }"
      >
        <div class="stage-name">{{ s.name }}</div>
        <div class="stage-desc">{{ s.desc }}</div>
      </div>
    </div>
    <div class="controls">
      <button @click="step">单步</button>
      <button v-if="!playing" @click="play">播放</button>
      <button v-else @click="pause">暂停</button>
      <button @click="reset">重置</button>
    </div>
    <p class="hint">CPU 周而复始地重复这个循环，每秒数百万次。</p>
  </div>
</template>

<style scoped>
.pipeline {
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  padding: 1.2rem;
  margin: 1.5rem 0;
  background: var(--vp-c-bg-soft);
}
.stages {
  display: flex;
  gap: 0.8rem;
}
.stage {
  flex: 1;
  border: 2px solid var(--vp-c-divider);
  border-radius: 10px;
  padding: 0.9rem;
  transition: all 0.3s ease;
  background: var(--vp-c-bg);
}
.stage.active {
  border-color: #00d4aa;
  background: rgba(0, 212, 170, 0.14);
  box-shadow: 0 0 0 3px rgba(0, 212, 170, 0.2);
  transform: translateY(-4px);
}
.stage-name {
  font-weight: 700;
  color: #00d4aa;
  margin-bottom: 0.4rem;
}
.stage-desc {
  font-size: 0.85rem;
  color: var(--vp-c-text-2);
  line-height: 1.5;
}
.controls {
  display: flex;
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
.controls button:hover {
  background: rgba(0, 212, 170, 0.12);
}
.hint {
  margin-top: 0.8rem;
  font-size: 0.85rem;
  color: var(--vp-c-text-3);
}
@media (max-width: 640px) {
  .stages { flex-direction: column; }
}
</style>
