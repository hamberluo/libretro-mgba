<script setup>
import { ref, reactive, computed } from 'vue'

const channels = reactive([
  { name: '方波 1', kind: 'PSG', value: 18, on: true },
  { name: '方波 2', kind: 'PSG', value: -12, on: true },
  { name: '波形',   kind: 'PSG', value: 9,  on: true },
  { name: '噪声',   kind: 'PSG', value: -6, on: true },
  { name: 'FIFO A', kind: 'FIFO', value: 40, on: true },
  { name: 'FIFO B', kind: 'FIFO', value: -28, on: true },
])

function toggle(i) { channels[i].on = !channels[i].on }
function reset() { channels.forEach(c => c.on = true) }

const output = computed(() => channels.reduce((s, c) => s + (c.on ? c.value : 0), 0))
const barWidth = computed(() => Math.min(100, Math.abs(output.value)))
const barPositive = computed(() => output.value >= 0)
</script>

<template>
  <div class="mixer">
    <div class="chans">
      <div
        v-for="(c, i) in channels"
        :key="i"
        class="chan"
        :class="[c.kind.toLowerCase(), { on: c.on }]"
        @click="toggle(i)"
      >
        <span class="cname">{{ c.name }}</span>
        <span class="ckind">{{ c.kind }}</span>
        <span class="cval">{{ c.on ? (c.value >= 0 ? '+' + c.value : c.value) : '静音' }}</span>
      </div>
    </div>
    <div class="out">
      <div class="olabel">混音输出 = 各开启声道相加</div>
      <div class="obar-wrap">
        <div class="obar" :class="{ neg: !barPositive }" :style="{ width: barWidth + '%' }"></div>
      </div>
      <div class="oval">采样值 <b>{{ output >= 0 ? '+' + output : output }}</b></div>
    </div>
    <div class="controls">
      <button @click="reset">全部开启</button>
    </div>
    <p class="hint">教学示意。点声道可静音/开启——混音的本质就是把各路波形的数值加起来。青绿是 4 个 PSG 声道（GB 继承），蓝色是 2 个 FIFO 声道（GBA 新增，DMA 喂数）。</p>
  </div>
</template>

<style scoped>
.mixer { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.chans { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 0.6rem; }
.chan {
  display: flex; flex-direction: column; gap: 0.3rem; padding: 0.6rem 0.8rem;
  border: 1px solid var(--vp-c-divider); border-radius: 8px; cursor: pointer;
  transition: all 0.2s ease; opacity: 0.45;
}
.chan.on { opacity: 1; }
.chan.psg.on { border-color: #00d4aa; background: rgba(0, 212, 170, 0.14); }
.chan.fifo.on { border-color: #5b8def; background: rgba(91, 141, 239, 0.14); }
.cname { font-size: 0.85rem; color: var(--vp-c-text-1); }
.ckind { font-size: 0.7rem; color: var(--vp-c-text-3); }
.cval { font-size: 0.85rem; font-family: var(--vp-font-family-mono); color: #00d4aa; white-space: nowrap; }
.chan.fifo .cval { color: #5b8def; }
.out { margin-top: 1.2rem; }
.olabel { font-size: 0.8rem; color: var(--vp-c-text-3); margin-bottom: 0.4rem; }
.obar-wrap { height: 16px; background: var(--vp-c-bg); border: 1px solid var(--vp-c-divider); border-radius: 8px; overflow: hidden; }
.obar { height: 100%; background: #00d4aa; transition: width 0.3s ease; }
.obar.neg { background: #ff6b6b; }
.oval { margin-top: 0.4rem; font-size: 0.9rem; color: var(--vp-c-text-2); }
.oval b { color: #00d4aa; font-family: var(--vp-font-family-mono); white-space: nowrap; }
.controls { display: flex; gap: 0.6rem; margin-top: 1rem; }
.controls button {
  padding: 0.4rem 1rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.9rem; transition: background 0.2s;
}
.controls button:hover { background: rgba(0, 212, 170, 0.12); }
.hint { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
</style>
