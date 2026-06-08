# 没有真 BIOS，游戏怎么还能跑？

<AudioPlayer src="audio/ep08-bios.mp3" />

第 7 集，我们走完了「一帧画面是怎么诞生的」整条主线。但有件事一直没解释：很多人用模拟器，**根本没有真正的 GBA BIOS 文件**，游戏却照样能跑。BIOS 是开机最先运行的系统固件，没有它，机器凭什么启动、游戏凭什么调用系统功能？

这一集，我们打开 `bios.c`，看 mGBA 怎么「假装」自己有 BIOS。

## 一、游戏怎么「请求」BIOS：SWI 软件中断

游戏需要 BIOS 帮忙时（比如做个除法——ARM7 没有硬件除法指令），它不会直接跳到 BIOS 里某个地址，而是执行一条特殊指令：`SWI 编号`（软件中断，第 3 集见过 `cpu->irqh.swi16`）。

「编号」表明它要哪个服务，这是一套约定好的「系统调用表」：

- `0x06` → 除法（Div）
- `0x08` → 平方根（Sqrt）
- `0x0B` → CpuSet（内存拷贝）
- …还有解压、三角函数、等待中断等等

游戏说「我要 0x06」，至于这个服务**怎么实现**，它不关心——这层抽象，正是 HLE 能介入的地方。

## 二、HLE：拦下编号，用 C 直接给结果

来看 mGBA 怎么处理这条 SWI：

```c
void GBASwi16(struct ARMCore* cpu, int immediate) {
	struct GBA* gba = (struct GBA*) cpu->master;

	if (gba->memory.fullBios) {       // 如果挂载了真 BIOS
		ARMRaiseSWI(cpu);             // 走真路径：跳进真 BIOS 的 ARM 代码
		return;
	}

	switch (immediate) {              // 否则 HLE：按编号，用 C 直接实现
	case GBA_SWI_DIV:
		_Div(gba, cpu->gprs[0], cpu->gprs[1]);   // 除法：商写回 r0、余写 r1
		break;
	case GBA_SWI_SQRT:
		cpu->gprs[0] = _Sqrt(cpu->gprs[0], &gba->biosStall);
		break;
	// ... CpuSet 内存拷贝、ArcTan、LZ77 解压 等
	}
}
```

> ↗ 源码：[`src/gba/bios.c#L408`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/bios.c#L408)

`switch (immediate)` 拦下编号，**不去跑真 BIOS 那几十条 ARM 指令**，而是直接调一个 C 函数（`_Div`）算出商和余数、写回寄存器。游戏拿到的结果一模一样，但模拟器只执行了一个宿主函数。

这就是 **HLE（High-Level Emulation，高级模拟）：模拟「要做什么」，不模拟「怎么一步步做」。**

## 三、两条路并存，所以不需要真 BIOS

注意最上面那个判断：

```c
if (gba->memory.fullBios) {
	ARMRaiseSWI(cpu);   // 有真 BIOS：逐指令跑它（LLE）
	return;
}
// 否则走下面的 HLE
```

- 你**提供了**真 BIOS ROM → `ARMRaiseSWI`，老老实实跳进 BIOS 的 ARM 代码逐条执行，这叫 **LLE（低级模拟）**；
- 你**没有** → 走 HLE，用 C 顶上。

正是这条 HLE 路，让模拟器**不依赖受版权保护的 BIOS ROM** 也能跑游戏。（`hle-bios.c` 里还有一小段手写的 ARM 字节桩，补上中断向量这类少数没法用纯 C 替代的部分——但绝大多数系统调用都被 HLE 接管了。）

## 四、HLE vs LLE：一个经典权衡

| | HLE 高级模拟 | LLE 低级模拟 |
|---|---|---|
| 做法 | 识别「要做什么」，宿主代码直接实现 | 忠实逐指令跑真硬件代码 |
| 速度 | 快 | 慢 |
| 是否需要 BIOS ROM | 不需要 | 需要 |
| 精确度 | 可能有细微差异（边角行为、时序） | 最高 |

mGBA 两者都支持，按有没有真 BIOS 自动选。这是模拟器设计里反复出现的权衡：**要极致精确，还是要够用又自由。**

## 五、动手试试：一次 Div 的 HLE 处理

下面这个组件，让游戏请求一次除法（`swi 0x06`），单步看 mGBA 怎么拦截、用 C 函数直接算出商和余、写回寄存器：

<SwiCallDemo />

## 下一集

主线走完了，BIOS 之谜也解开了。还剩最后一块拼图：你看到的这一帧画面，配的那段声音，是怎么生成、又怎么和画面同步发出来的？下一集，《声音：4+2 个声道如何合成一帧音频》。
