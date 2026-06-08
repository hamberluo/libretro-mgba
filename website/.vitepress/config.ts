import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'GBA 模拟器内核精讲',
  description: '基于 mGBA 源码，深入浅出讲解 GBA 模拟器内核',
  lang: 'zh-CN',
  base: '/libretro-mgba/',
  cleanUrls: true,
  appearance: 'dark',
  themeConfig: {
    nav: [
      { text: '首页', link: '/' },
      { text: '序章', link: '/guide/intro' },
    ],
    sidebar: [
      {
        text: '系列',
        items: [
          { text: '序章 · 一帧画面是怎么诞生的', link: '/guide/intro' },
          { text: 'CPU · 软件怎么假装成一块 ARM7 芯片', link: '/guide/ep02-cpu' },
          { text: '一条指令的执行 · Thumb 指令集实战', link: '/guide/ep03-thumb' },
          { text: '内存不是数组 · MMIO 与地址映射', link: '/guide/ep04-memory' },
          { text: '时间的主宰 · 周期精确与事件调度', link: '/guide/ep05-timing' },
          { text: 'DMA · 不打扰 CPU 的搬运工', link: '/guide/ep06-dma' },
          { text: 'PPU · 扫描线是怎么画出来的', link: '/guide/ep07-ppu' },
          { text: '没有真 BIOS，游戏怎么还能跑？', link: '/guide/ep08-bios' },
          { text: '声音 · 4+2 个声道如何合成一帧音频', link: '/guide/ep09-audio' },
        ],
      },
    ],
    search: { provider: 'local' },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/hamberluo/libretro-mgba' },
    ],
    outline: { level: [2, 3], label: '本页目录' },
    docFooter: { prev: false, next: false },
  },
})
