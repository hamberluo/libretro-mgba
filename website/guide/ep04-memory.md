# 内存不是数组 · MMIO 与地址映射

<AudioPlayer src="audio/ep04-memory.mp3" />

上一集结尾，访存指令把额外的周期写进了 `currentCycles`：

```c
cpu->gprs[rd] = cpu->memory.load32(cpu, cpu->gprs[rm] + immediate * 4, &currentCycles);
```

这一集我们打开 `load32`，回答两个问题：一次内存访问到底花几个周期？以及——内存为什么不是一个简单的大数组？

## 一、内存不是大数组，是一张路由表

很多人以为模拟器的内存就是 `uint8_t mem[0x10000000]` 这样一个大数组，读写就是下标访问。打开 `GBALoad32` 你会发现完全不是：

```c
uint32_t GBALoad32(struct ARMCore* cpu, uint32_t address, int* cycleCounter) {
	struct GBA* gba = (struct GBA*) cpu->master;
	struct GBAMemory* memory = &gba->memory;
	uint32_t value = 0;
	int wait = 0;

	switch (address >> BASE_OFFSET) {
	case GBA_REGION_BIOS:        LOAD_BIOS;        break;
	case GBA_REGION_EWRAM:       LOAD_EWRAM;       break;
	case GBA_REGION_IWRAM:       LOAD_IWRAM;       break;
	case GBA_REGION_IO:          LOAD_IO;          break;
	case GBA_REGION_PALETTE_RAM: LOAD_PALETTE_RAM; break;
	case GBA_REGION_VRAM:        LOAD_VRAM;        break;
	case GBA_REGION_OAM:         LOAD_OAM;         break;
	case GBA_REGION_ROM0:        LOAD_CART;        break;
	case GBA_REGION_SRAM:        LOAD_SRAM;        break;
	default:                     LOAD_BAD;         break;
	}
	// ... 计算 wait，写回 cycleCounter
}
```

> ↗ 源码：[`src/gba/memory.c#L476`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/memory.c#L476)

关键是第一行 `switch (address >> BASE_OFFSET)`。`BASE_OFFSET` 是 24，所以 `address >> 24` 取的是地址的**最高一个字节**——这就是「区域号」。一个 32 位地址，高 8 位决定了你在跟哪块硬件说话：

| 区域号（address>>24） | 区域 |
|---|---|
| 0x0 | BIOS |
| 0x2 | EWRAM（外部工作内存） |
| 0x3 | IWRAM（内部工作内存） |
| 0x4 | IO 寄存器 |
| 0x5 | 调色板 RAM |
| 0x6 | VRAM（显存） |
| 0x7 | OAM（精灵属性） |
| 0x8 | 卡带 ROM |
| 0xE | SRAM（存档） |

> ↗ 区域定义：[`include/mgba/internal/gba/memory.h#L24`](https://github.com/hamberluo/libretro-mgba/blob/master/include/mgba/internal/gba/memory.h#L24)

每个 `case` 背后是一块**独立的存储 + 独立的读取逻辑**（那些 `LOAD_xxx` 宏）。内存不是一整块，而是一张按地址高位分发的**路由表**。

## 二、MMIO：有一块「内存」读的根本不是内存

注意 `GBA_REGION_IO`（0x4 段）这个分支，它走的是 `LOAD_IO`。这里读到的，**不是某块 RAM，而是硬件寄存器的当前值**——PPU 画到第几行了、定时器计到几了、哪个按键被按下了、DMA 在不在忙。

这就是 **MMIO（Memory-Mapped IO，内存映射输入输出）**：把硬件的控制和状态「假装」成内存地址，CPU 用普通的读写内存指令，就能和硬件对话。还记得序章说「按下 A 键会被记录到内存里一个固定位置」吗？那个位置（0x04000130，按键寄存器）就在 IO 区。游戏读这个地址，读到的就是当前按键状态——它以为在读内存，其实在问硬件。

这是模拟器里一个深刻的点：**地址不只是「数据在哪」，还可能是「跟谁说话」。**

## 三、周期从哪来（兑现上集的钩子）

回到上一集的问题：访存花的周期是哪来的？就在 `GBALoad32` 末尾：

```c
if (cycleCounter) {
	wait += 2;
	if (address < GBA_BASE_ROM0) {
		wait = GBAMemoryStall(cpu, wait);
	}
	*cycleCounter += wait;
}
```

`wait` 的基础值来自一张表 `waitstatesNonseq32[region]`——**每个区域访问快慢不同**：

- **IWRAM**（内部工作内存）：32 位总线，最快，游戏把热点数据和代码放这里；
- **EWRAM**（外部工作内存）：16 位总线，读一个 32 位值要两拍，较慢；
- **卡带 ROM**：速度取决于卡带的 waitstate 配置，可能很慢；
- 此外 `GBAMemoryStall` 还会加上预取单元争用总线导致的额外停顿。

最后 `*cycleCounter += wait`——把这次访问花的周期，加回上一集那个 `currentCycles`。这就是闭环：**指令说"我要读内存"，内存系统按地址落在哪个区域，告诉它"这要花几拍"。** 周期精确，精确到每一次访存落在哪块硬件上。

## 四、动手试试：地址落在哪、有多快

下面这个组件，选一个地址，看它的高 8 位如何决定它落进 GBA 内存地图的哪一块，以及那块有多快：

<MemoryMapDemo />

## 下一集

我们已经看到，CPU 执行指令、访存，每一步都在累加周期。但是——CPU 一路跑下去，PPU 什么时候该画下一行？定时器什么时候该响？是谁在协调这些部件，让它们在正确的周期点上各司其职？

下一集，《时间的主宰：周期精确与事件调度》，我们去看那根「贯穿全场的时钟」到底是怎么实现的。
