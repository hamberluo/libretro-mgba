# 一条指令的执行 · Thumb 指令集实战

<AudioPlayer src="audio/ep03-thumb.mp3" />

上一集，CPU 解码的终点是 `instruction(cpu, opcode)`——调用一个函数。这一集，我们打开这个函数，看一条指令到底怎么改变 CPU 的状态，以及它消耗的**周期究竟从哪里来**。

GBA 游戏的代码绝大多数跑在 **Thumb 模式**：16 位一条指令，比 32 位的 ARM 指令更省空间。我们就从 Thumb 看起。

## 一、宏，就是一台代码生成器

打开 `isa-thumb.c`，你会发现几乎看不到普通的函数定义，全是宏。每一条 Thumb 指令的实现，都被这个宏包起来：

```c
#define DEFINE_INSTRUCTION_THUMB(NAME, BODY) \
	static void _ThumbInstruction ## NAME (struct ARMCore* cpu, unsigned opcode) {  \
		int currentCycles = THUMB_PREFETCH_CYCLES; \
		BODY; \
		cpu->cycles += currentCycles; \
	}
```

> ↗ 源码：[`src/arm/isa-thumb.c#L58`](https://github.com/hamberluo/libretro-mgba/blob/master/src/arm/isa-thumb.c#L58)

`## NAME` 是预处理器的拼接：传入 `ADD3`，就生成一个名叫 `_ThumbInstructionADD3` 的函数。这个函数的签名 `(struct ARMCore* cpu, unsigned opcode)`——正是上一集 `_thumbTable` 里存的那种函数指针。**查表查到的，就是这里被宏生成出来的函数。** 预处理器在这里扮演了一台代码生成器：几百条指令，不用手写几百个函数壳，宏统一生成。

## 二、周期从哪来（兑现上集的钩子）

上一集留了个问题：周期是怎么算的？ARMStep 里只看到查表和调用，没看到累加。答案就在这个宏的一头一尾：

```c
int currentCycles = THUMB_PREFETCH_CYCLES;  // 开头：基础取指周期
BODY;                                        // 中间：指令本体
cpu->cycles += currentCycles;                // 结尾：记账
```

**每条指令自己负责记自己的账。** 一条普通运算指令花基础周期；而访存指令会在 `BODY` 里把额外的内存周期加进 `currentCycles`（下一集细讲）。所以 ARMStep 不需要管周期——它只管驱动循环，计时分散在每条指令实现里。这就是「周期精确」落到代码上的样子。

## 三、解码操作数：从 16 位里抠寄存器号

我们的主角是 `ADD3`（寄存器加寄存器）。它用一个中间宏先把操作数解码出来：

```c
#define DEFINE_DATA_FORM_1_INSTRUCTION_THUMB(NAME, BODY) \
	DEFINE_INSTRUCTION_THUMB(NAME, \
		int rm = (opcode >> 6) & 0x0007; \
		int rd = opcode & 0x0007; \
		int rn = (opcode >> 3) & 0x0007; \
		BODY;)
```

> ↗ 源码：[`src/arm/isa-thumb.c#L112`](https://github.com/hamberluo/libretro-mgba/blob/master/src/arm/isa-thumb.c#L112)

16 位 opcode 里，三个 3 位字段分别是目标寄存器 `rd`、操作数 `rn` 和 `rm`（`& 0x7` 取 3 位，范围 0-7）。注意——和上一集说的一样，这些都只是 `gprs[]` 数组的**下标**。指令不操作什么神秘的硬件，它操作的是一个数组的几个格子。

## 四、ADD3 本体：一行代码

有了操作数，ADD3 的本体就一行：

```c
DEFINE_DATA_FORM_1_INSTRUCTION_THUMB(ADD3, THUMB_ADDITION(cpu->gprs[rd], cpu->gprs[rn], cpu->gprs[rm]))
```

> ↗ 源码：[`src/arm/isa-thumb.c#L119`](https://github.com/hamberluo/libretro-mgba/blob/master/src/arm/isa-thumb.c#L119)

`THUMB_ADDITION` 宏做两件事——相加，然后设置标志位：

```c
#define THUMB_ADDITION(D, M, N) \
	int n = N; \
	int m = M; \
	D = M + N; \
	THUMB_ADDITION_S(m, n, D)
```

`gprs[rd] = gprs[rn] + gprs[rm]`，加法本身就是 C 的 `+`。真正有讲究的是后半句——更新标志位。

## 五、标志位：一条指令留给下一条的线索

```c
#define THUMB_ADDITION_S(M, N, D) \
	cpu->cpsr.flags = 0; \
	cpu->cpsr.n = ARM_SIGN(D); \
	cpu->cpsr.z = !(D); \
	cpu->cpsr.c = ARM_CARRY_FROM(M, N, D); \
	cpu->cpsr.v = ARM_V_ADDITION(M, N, D);
```

> ↗ 源码：[`src/arm/isa-thumb.c#L14`](https://github.com/hamberluo/libretro-mgba/blob/master/src/arm/isa-thumb.c#L14)

四个标志位，记录这次加法的「副作用」：

- **N**（Negative）：结果最高位，即结果是否为负；
- **Z**（Zero）：结果是否为零；
- **C**（Carry）：无符号运算是否进位（结果绕回了）；
- **V**（oVerflow）：有符号运算是否溢出（两个同号数相加，结果却变了号）。

还记得上一集的条件执行吗？那里查 `conditionLut` 用的 `cpsr.flags`，正是这里设下的。**一条指令算完设标志，下一条带条件的指令据此决定要不要执行**——这就是闭环：ADD 不只是算个和，它还为后面的 `BEQ`（相等则跳转）之类留下了线索。

## 六、动手试试：同一条 ADD，不同的标志位

下面这个组件，让你选不同的输入预设，单步走一遍 ADD3——看同一条加法指令，在「正常」「进位」「溢出」三种输入下，N/Z/C/V 怎么被设成不同的值。

<ThumbAddDemo />

## 下一集

第五节我们提到，访存指令会把额外周期写进 `currentCycles`：

```c
cpu->gprs[rd] = cpu->memory.load32(cpu, cpu->gprs[rm] + immediate * 4, &currentCycles);
```

一次内存访问，到底花几个周期？为什么有的地址快、有的地址慢？内存在模拟器里又凭什么不是一个简单的大数组？下一集，《内存不是数组：MMIO 与地址映射》。
