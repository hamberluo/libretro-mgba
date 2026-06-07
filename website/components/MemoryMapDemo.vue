<script setup>
import { ref, computed } from 'vue'

const regions = [
  { region: 0x0, name: 'BIOS',        base: '0x00000000', speed: '系统固件，只读' },
  { region: 0x2, name: 'EWRAM',       base: '0x02000000', speed: '外部工作内存，较慢（16 位总线）' },
  { region: 0x3, name: 'IWRAM',       base: '0x03000000', speed: '内部工作内存，最快（32 位总线）' },
  { region: 0x4, name: 'IO 寄存器',   base: '0x04000000', speed: 'MMIO——读的是硬件寄存器，不是 RAM' },
  { region: 0x5, name: '调色板 RAM',  base: '0x05000000', speed: '调色板数据' },
  { region: 0x6, name: 'VRAM',        base: '0x06000000', speed: '显存——画面数据' },
  { region: 0x7, name: 'OAM',         base: '0x07000000', speed: '精灵属性表' },
  { region: 0x8, name: '卡带 ROM',    base: '0x08000000', speed: '游戏卡带，速度取决于 waitstate 配置' },
  { region: 0xE, name: 'SRAM',        base: '0x0E000000', speed: '存档用，慢' },
]

const presets = [
  { label: 'BIOS',  addr: 0x00000000 },
  { label: 'EWRAM', addr: 0x02000000 },
  { label: 'IWRAM', addr: 0x03000000 },
  { label: 'IO',    addr: 0x04000000 },
  { label: 'VRAM',  addr: 0x06000000 },
  { label: 'ROM',   addr: 0x08000000 },
  { label: 'SRAM',  addr: 0x0E000000 },
]

const sel = ref(2)

const curAddr = computed(() => presets[sel.value].addr)
const curRegionNum = computed(() => (curAddr.value >>> 24))
const curRegion = computed(() => regions.find(r => r.region === curRegionNum.value))
const addrHex = computed(() => '0x' + (curAddr.value >>> 0).toString(16).toUpperCase().padStart(8, '0'))
const regionHex = computed(() => '0x' + curRegionNum.value.toString(16).toUpperCase())
</script>

<template>
  <div class="memmap">
    <div class="presets">
      <button
        v-for="(p, i) in presets"
        :key="i"
        :class="{ on: i === sel }"
        @click="sel = i"
      >{{ p.label }}</button>
    </div>
    <div class="map">
      <div
        v-for="r in regions"
        :key="r.region"
        class="block"
        :class="{ hit: r.region === curRegionNum }"
      >
        <span class="addr">{{ r.base }}</span>
        <span class="rname">{{ r.name }}</span>
      </div>
    </div>
    <div class="info">
      <div class="iline">地址 <b>{{ addrHex }}</b></div>
      <div class="iline">取高 8 位 <b>{{ addrHex }} >> 24 = {{ regionHex }}</b></div>
      <div class="iline">命中区域 <b>{{ curRegion ? curRegion.name : '未映射' }}</b></div>
      <div class="iline speed">{{ curRegion ? curRegion.speed : '' }}</div>
    </div>
    <p class="hint">教学示意，地址范围简化。一个 32 位地址，高 8 位就决定了它落在哪块硬件上。</p>
  </div>
</template>

<style scoped>
.memmap { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.presets { display: flex; gap: 0.5rem; margin-bottom: 1rem; flex-wrap: wrap; }
.presets button {
  padding: 0.35rem 0.9rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.85rem; transition: background 0.2s;
}
.presets button.on { background: #00d4aa; color: #0d1b2a; font-weight: 700; }
.presets button:hover { background: rgba(0, 212, 170, 0.12); }
.presets button.on:hover { background: #1ae0b8; color: #0d1b2a; }
.map { display: flex; flex-direction: column; gap: 0.3rem; margin-bottom: 1rem; }
.block {
  display: flex; justify-content: space-between; align-items: baseline; gap: 1rem;
  padding: 0.5rem 0.8rem; border: 1px solid var(--vp-c-divider); border-radius: 8px;
  transition: all 0.25s ease;
}
.block.hit { border-color: #00d4aa; background: rgba(0, 212, 170, 0.14); box-shadow: inset 3px 0 0 #00d4aa; }
.block .addr { font-family: var(--vp-font-family-mono); font-size: 0.8rem; color: var(--vp-c-text-3); white-space: nowrap; flex-shrink: 0; }
.block .rname { color: var(--vp-c-text-1); font-size: 0.9rem; text-align: right; }
.info { display: flex; flex-direction: column; gap: 0.4rem; padding: 0.8rem; border: 1px solid var(--vp-c-divider); border-radius: 8px; }
.iline { font-size: 0.9rem; color: var(--vp-c-text-2); }
.iline b { color: #00d4aa; font-family: var(--vp-font-family-mono); white-space: nowrap; }
.iline.speed { font-size: 0.85rem; color: var(--vp-c-text-3); }
.hint { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
</style>
