# DMA · 不打扰 CPU 的搬运工

第 5 集那条事件队列里，有一类事件叫「DMA 传输」。序章说它是个「不打扰 CPU 的高速搬运工」。可一个部件凭什么能「抢」过 CPU、占用内存总线？这一集我们打开 `GBADMAService`，看它一次传输到底做了什么。

## 一、抢总线的真相：直接把 CPU 摁住

先看最关键的几行：

```c
void GBADMAService(struct GBA* gba, int number, struct GBADMA* info) {
	struct GBAMemory* memory = &gba->memory;
	struct ARMCore* cpu = gba->cpu;
	uint32_t source = info->nextSource;
	uint32_t dest = info->nextDest;
	int32_t cycles = 2;

	gba->cpuBlocked = true;                  // 关键：直接阻塞 CPU
	gba->performingDMA = 1 | (number << 1);
	cpu->memory.accessSource = mACCESS_DMA;  // 此刻总线归 DMA
	// ...
}
```

> ↗ 源码：[`src/gba/dma.c#L263`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/dma.c#L263)

`gba->cpuBlocked = true`——这就是答案。DMA 并不是「和 CPU 并行、互不打扰」，而是**把 CPU 挂起，自己独占内存总线**。`accessSource = mACCESS_DMA` 标明：此刻总线上跑的是 DMA，不是 CPU。搬完，再把 CPU 放回去。所谓「不打扰」，准确说是**「短暂霸占、但很快归还」**。

## 二、搬运本体：就是 load + store + 步进

DMA 怎么搬？没有魔法，就是读一个、写一个：

```c
if (width == 4) {
	info->latch = cpu->memory.load32(cpu, source, 0);   // 从源读一个字
	cpu->memory.store32(cpu, dest, info->latch, 0);     // 写到目标
}
// ...
info->nextSource += info->sourceOffset;   // 源地址步进
info->nextDest += info->destOffset;       // 目标地址步进
```

`load32(source)` 和 `store32(dest, ...)`——**正是第 4 集那对 load/store**，连带着第 4 集的地址路由、waitstate 全都复用。读完写完，源和目标地址各自按配置步进（递增 / 递减 / 固定），重复，直到搬完设定的字数。

所以 DMA 的本质，是一个**不需要 CPU 逐条指令驱动的自动 load-store 循环**。CPU 只需在开头配置好「从哪搬、搬到哪、搬多少」，剩下的硬件自己跑。

## 三、周期账：DMA 也要记时间

DMA 占着总线，自然也要花周期：

```c
// 按源/目标区域的 waitstate 累加
cycles += memory->waitstatesNonseq32[sourceRegion] + memory->waitstatesNonseq32[destRegion];
info->when += cycles;   // 占用的周期，记进时间线
```

`waitstatesNonseq32[region]`——**又是第 4 集的 waitstate**（访问越慢的区域，DMA 也越慢）；`info->when += cycles`——**又是第 5 集的事件 when**。DMA 本身就是登记在第 5 集那条事件队列里的一个事件，它霸占总线的这些周期，精确地记在时间线上。第 4 集、第 5 集、这一集，在这里汇合。

## 四、那它到底「打不打扰」CPU？

诚实地说：**搬运期间 CPU 确实停了**，`cpuBlocked` 就是证据。那为什么大家都叫它「不打扰的搬运工」？两个原因：

1. **快**：它走专用硬件路径，比 CPU 用普通指令一个字一个字搬要快得多，霸占总线的时间很短；
2. **挑时机**：DMA 常被安排在 **HBlank（一行画完的间隙）、VBlank（一帧画完的间隙）** 这种 CPU 本来就在等画面、没多少事做的空当触发。

所以它**显得**像「后台默默搬运」，本质是「高效 + 挑时机的短暂霸占」。这也是为什么游戏喜欢用 DMA 在 VBlank 把下一帧的图像数据批量搬进显存——又快又不耽误正事。

## 五、动手试试：看 DMA 搬运与 CPU 阻塞

下面这个组件，单步把源数据一个字一个字搬到目标。注意中间的 CPU 状态——搬运期间它是「已阻塞」，搬完才恢复运行：

<DmaTransferDemo />

## 下一集

DMA 最常干的活，就是把图像数据搬进 **VRAM（显存）**。可数据进了显存，它还只是一堆字节——怎么变成屏幕上你看到的一行行像素？下一集，《PPU：扫描线是怎么画出来的》。
