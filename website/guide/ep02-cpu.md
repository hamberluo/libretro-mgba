# CPU · 软件怎么假装成一块 ARM7 芯片

<AudioPlayer src="audio/ep02-cpu.mp3" />

上一集我们说，CPU 在不停地重复一个循环：取指、解码、执行。那只是个直觉。这一集，我们看它在 mGBA 里**真实的样子**——你会发现，整个循环就浓缩在一个函数里。

## 一、先看全貌：11 行就是 CPU 的灵魂

GBA 的 CPU 是一颗 ARM7，运行在 ARM 模式下时，mGBA 用这个函数走完一条指令：

```c
static inline void ARMStep(struct ARMCore* cpu) {
	uint32_t opcode = cpu->prefetch[0];
	cpu->prefetch[0] = cpu->prefetch[1];
	cpu->gprs[ARM_PC] += WORD_SIZE_ARM;
	LOAD_32(cpu->prefetch[1], cpu->gprs[ARM_PC] & cpu->memory.activeMask, cpu->memory.activeRegion);

	unsigned condition = opcode >> 28;
	if (condition != 0xE) {
		unsigned flags = cpu->cpsr.flags >> 4;
		bool conditionMet = conditionLut[condition] & (1 << flags);
		if (!conditionMet) {
			cpu->cycles += ARM_PREFETCH_CYCLES;
			return;
		}
	}
	ARMInstruction instruction = _armTable[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0x00F)];
	instruction(cpu, opcode);
}
```

> ↗ 源码：[`src/arm/arm.c#L201`](https://github.com/hamberluo/libretro-mgba/blob/master/src/arm/arm.c#L201)

别被它吓到。这 11 行，包含了一颗 CPU 全部的灵魂：取指、流水线、PC、条件执行、解码、执行、计时。我们一个个拆。

## 二、取指与流水线：PC 为什么总是「超前」

```c
uint32_t opcode = cpu->prefetch[0];
cpu->prefetch[0] = cpu->prefetch[1];
cpu->gprs[ARM_PC] += WORD_SIZE_ARM;
LOAD_32(cpu->prefetch[1], ...);
```

真实的 ARM7 芯片用三级流水线：当前指令在执行时，下一条已经在解码、再下一条已经在取指。mGBA 没有逐级精确模拟流水线，而是用一个**两格的预取队列** `prefetch[0]`、`prefetch[1]` 抓住它的**行为**：

- `prefetch[0]` 是这次要执行的指令；
- 取走它之后，`prefetch[1]` 递补到 `prefetch[0]`；
- `PC` 前进一条指令的宽度（ARM 模式 4 字节）；
- 再从新的 `PC` 处 `LOAD` 一条，填满 `prefetch[1]`。

结果就是：**PC 永远领先正在执行的指令两条**。这正是真实硬件的可观测行为——很多游戏代码会利用「读 PC 拿到的是当前指令+8」这个特性。软件不必复刻电路，只要这个行为对得上，游戏就分辨不出真假。这就是「假装成芯片」的精髓。

## 三、PC 只是数组里的一个格子

`cpu->gprs[ARM_PC]` 揭示了一件事：所谓寄存器组，在模拟器里就是一个数组 `gprs`，而 PC（程序计数器）不过是其中一个有特殊约定的格子（`ARM_PC` 是它的下标）。改 PC = 改数组的一个元素，跳转就是给这个格子赋个新值。CPU 的「状态」，本质就是这一组数字。

## 四、条件执行：ARM 的独门设计

```c
unsigned condition = opcode >> 28;
if (condition != 0xE) {
	unsigned flags = cpu->cpsr.flags >> 4;
	bool conditionMet = conditionLut[condition] & (1 << flags);
	if (!conditionMet) {
		cpu->cycles += ARM_PREFETCH_CYCLES;
		return;
	}
}
```

ARM 指令有个独特设计：**几乎每条指令的最高 4 位都是「条件码」**。`0xE` 表示「无条件执行」（最常见，所以单独快速放行）；否则就拿这 4 位条件码，去 `conditionLut` 这张表里查——结合当前的标志位（`cpsr.flags`，记录上次运算是否为零、有无进位等），判断这条指令这次到底要不要执行。

为什么这么设计？为了**少跳转**。别的架构里 `if (x) y++` 往往要一条分支指令；ARM 可以让 `y++` 自带「仅当上次比较相等才执行」的条件，省掉分支，流水线不被打断。

> ↗ 条件查找表 `conditionLut`：[`src/arm/arm.c#L182`](https://github.com/hamberluo/libretro-mgba/blob/master/src/arm/arm.c#L182)

注意最后那句 `cpu->cycles += ARM_PREFETCH_CYCLES; return;`：**即使这条指令被跳过，也要计时、也要花周期**。这呼应了序章说的「周期精确」——时间账一分都不能少记。

## 五、解码就是查一张表（兑现上集的钩子）

序章我们卖了个关子：解码用「查表法」。这就是它：

```c
ARMInstruction instruction = _armTable[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0x00F)];
```

`_armTable` 是一张**预先建好的函数指针表**。mGBA 不用一长串 `if (是加法) ... else if (是跳转) ...` 去判断指令类型，而是把 opcode 里真正决定指令种类的那些**特征位**抠出来（`(opcode >> 16) & 0xFF0` 取高位特征，`(opcode >> 4) & 0x00F` 取低位特征），拼成一个索引，**一次查表**就拿到这条指令对应的处理函数。O(1)，没有分支链。

这是模拟器里反复出现的核心手法：**用空间换时间，用表驱动代替条件判断**。一张表把「指令长什么样」和「该怎么执行」一一对应起来。

## 六、执行：交给函数指针

```c
instruction(cpu, opcode);
```

查表拿到的 `instruction` 是个函数指针，调用它，就真正执行了这条指令——读写寄存器、算术运算、访存、跳转。具体每条指令内部怎么实现（比如一条加法怎么更新标志位），是**下一集**的内容。这一集你只需记住：解码的终点，是调用一个专门写好的函数。

## 七、单步感受一下

光看代码不够直观。下面这个组件用一段示意指令序列，带你**单步**走一遍 `ARMStep`：左边高亮当前执行到的代码行，右边是 CPU 的状态。重点盯住 `PC` 和 `prefetch`——你会亲眼看到 PC 怎么领先、预取队列怎么递补、cycles 怎么累加。

<ArmStepDemo />

## 八、这个函数被谁反复调用

`ARMStep` 只走一条指令。是谁让它转起来的？

```c
void ARMRunLoop(struct ARMCore* cpu) {
	while (cpu->cycles < cpu->nextEvent) {
		ARMStep(cpu);   // 一直跑，直到该处理下一个事件
	}
	cpu->irqh.processEvents(cpu);
}
```

> ↗ 源码：[`src/arm/arm.c#L243`](https://github.com/hamberluo/libretro-mgba/blob/master/src/arm/arm.c#L243)

注意循环条件 `cpu->cycles < cpu->nextEvent`：CPU 一条条执行指令、一路累加周期，**直到攒够周期、该轮到别的部件做事了**（比如 PPU 该画下一行、定时器该响），才停下来交给事件调度器。这正是序章那根「贯穿全场的时钟」如何指挥 CPU 的——我们会在第 5 集《时间的主宰》里专门讲这个调度器。

## 下一集

我们已经看到，解码的终点是 `instruction(cpu, opcode)` 调用一个函数。那个函数里到底发生了什么？一条指令是怎么真正改变 CPU 状态的？下一集，我们走进 **Thumb 指令集**，看一条指令如何执行、周期从哪里来。
