---
layout: home
hero:
  name: GBA 模拟器内核精讲
  text: 基于 mGBA 源码，深入浅出
  tagline: 跟着「一帧画面的诞生」，看懂一台 GBA 是怎么跑起来的
  actions:
    - theme: brand
      text: 开始阅读 · 序章
      link: /guide/intro
    - theme: alt
      text: GitHub
      link: https://github.com/hamberluo/libretro-mgba
features:
  - title: 序 · 一帧画面是怎么诞生的
    details: 全链路鸟瞰 —— 模拟器到底在模拟什么。
    link: /guide/intro
    linkText: 开始阅读
  - title: 02 · CPU：软件怎么假装成一块 ARM7 芯片
    details: 取指→解码→执行。难题：解码慢？查表法。
    link: /guide/ep02-cpu
    linkText: 开始阅读
  - title: 03 · 一条指令的执行：Thumb 指令集实战
    details: 执行→写回。难题：周期从哪来。
    link: /guide/ep03-thumb
    linkText: 开始阅读
  - title: 04 · 内存不是数组：MMIO 与地址映射
    details: CPU 读写内存。难题：一次访存几个周期。
    link: /guide/ep04-memory
    linkText: 开始阅读
  - title: 05 · 时间的主宰：周期精确与事件调度
    details: 贯穿全局的时钟。难题：怎么做到周期精确。
    link: /guide/ep05-timing
    linkText: 开始阅读
  - title: 06 · DMA：不打扰 CPU 的搬运工
    details: 内存→显存的高速搬运。难题：DMA 凭什么抢总线。
    link: /guide/ep06-dma
    linkText: 开始阅读
  - title: 07 · PPU：扫描线是怎么画出来的
    details: 显存→像素。难题：软件渲染 vs 硬件。
    link: /guide/ep07-ppu
    linkText: 开始阅读
  - title: 08 · 没有真 BIOS，游戏怎么还能跑？
    details: 启动与系统调用。难题：HLE 高级模拟。
    link: /guide/ep08-bios
    linkText: 开始阅读
  - title: 09 · 声音：4+2 个声道如何合成一帧音频
    details: 像素之外的另一条线。难题：音视频同步。
    link: /guide/ep09-audio
    linkText: 开始阅读
  - title: 10 · 随时存档读档：把整台机器冻在一瞬间
    details: 状态快照。难题：状态序列化的坑。
    link: /guide/ep10-savestate
    linkText: 开始阅读
---
