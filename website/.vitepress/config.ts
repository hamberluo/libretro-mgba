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
