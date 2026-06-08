# 随时存档读档 · 把整台机器冻在一瞬间

前 9 集，我们看着一台 GBA 在 mGBA 里一点点活了过来。这一集，也是最后一集，讲一个真机绝对做不到、模拟器却轻而易举的超能力：把一台**正在全速运行**的机器，精确地「冻」在某一瞬间，关掉再打开，还能分毫不差地接着跑。打开 `serialize.c`。

## 一、存档，就是把状态变量抄一份

`GBASerialize` 做的事，直白得让人意外：

```c
void GBASerialize(struct GBA* gba, struct GBASerializedState* state) {
	STORE_32(GBASavestateMagic + GBASavestateVersion, 0, &state->versionMagic);  // 魔数+版本
	STORE_32(gba->biosChecksum, 0, &state->biosChecksum);
	STORE_32(gba->timing.masterCycles, 0, &state->masterCycles);   // 第5集：主时钟

	int i;
	for (i = 0; i < 16; ++i) {
		STORE_32(gba->cpu->gprs[i], i * 4, state->cpu.gprs);       // 第2集：16 个寄存器
	}
	STORE_32(gba->cpu->cpsr.packed, 0, &state->cpu.cpsr.packed);  // 第3集：标志位
	STORE_32(gba->cpu->nextEvent, 0, &state->cpu.nextEvent);      // 第5集：下一个事件
	STORE_32(gba->cpu->prefetch[0], 0, state->cpuPrefetch);       // 第2集：流水线预取
	STORE_32(gba->cpu->prefetch[1], 4, state->cpuPrefetch);
	// ... 接着是 memory / video / audio / dma / timers 各子系统状态
}
```

> ↗ 源码：[`src/gba/serialize.c#L27`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/serialize.c#L27)

它把 CPU 的 16 个寄存器、标志位、下一个事件、流水线预取、主时钟，加上内存、视频、音频、DMA、定时器的当前值，逐个 `STORE_32` 写进一个大结构体 `GBASerializedState`。**这个结构体，就是某一瞬间整台机器的完整快照。** 读档（`GBADeserialize`）是它的镜像：把每个字段 `LOAD_32` 还原回部件。

## 二、为什么能「冻结」——因为状态就是一堆变量

留意上面那些注释。存档存的每一样东西，我们这一路都见过：

- `gprs[16]`、`prefetch` —— CPU 的寄存器和流水线（**第 2 集**）
- `cpsr` —— 运算标志位（**第 3 集**）
- 内存各区域的字节 —— （**第 4 集**）
- `masterCycles`、`nextEvent` —— 主时钟和事件队列（**第 5 集**）
- `vcount` —— PPU 画到第几行（**第 7 集**）
- 音频、DMA、定时器状态 —— （**第 6、9 集**）

关键就在这里：**整台 GBA，没有任何「藏在硬件深处、看不见摸不着」的状态——它的一切，都是 mGBA 里明明白白的 C 变量。** 所以「冻结」不需要什么黑魔法，只要把这些变量抄一份；「还原」只要把它们抄回去。

真机做不到这件事——你没法把一颗真 CPU 此刻每个晶体管的电平都记下来。但模拟器可以，因为它把硬件翻译成了变量。这是模拟器独有的超能力，也是「即时存档」「速通回放」「调试回溯」这些功能的根基。

## 三、versionMagic：序列化的经典坑

注意存档的第一个字段，是 `GBASavestateMagic + GBASavestateVersion`——一个魔数加版本号。

读档时，第一件事就是验证它：魔数不对，说明这根本不是个 mGBA 存档；版本不对，说明存档结构变了（新版本加了字段、调了布局）。这时候必须拒绝或走兼容路径——**否则拿旧存档去填新结构，字段全部错位，还原出来的是一台精神错乱的机器**：PC 指向乱码、时钟对不上、画面花屏。

这不是 GBA 特有的问题。**任何要把结构化状态存下来、以后再读回来的系统，都要给格式带上版本号。** 配置文件、存档、网络协议、数据库 schema——同一个坑，同一个解法。

## 四、动手试试：冻结再还原

下面这个组件，模拟了几个关键状态。点「运行一下」让它们变化，「存档」拍下快照，再「运行」让机器跑偏，最后「读档」——看每个状态被精确还原回快照那一刻：

<SaveStateDemo />

> ↗ 读档源码：[`src/gba/serialize.c#L96`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/serialize.c#L96)

## 尾声：一台 GBA 是怎么活过来的

十集走到这里，我们把一台 GBA 在 mGBA 里的一生，完整看了一遍：

- **序章**：一帧画面的诞生，是整台机器协同的结果；
- **CPU（第 2 集）**：用一个函数假装成一块 ARM7，取指、解码、执行；
- **指令（第 3 集）**：一条 Thumb 加法如何改变寄存器、留下标志位；
- **内存（第 4 集）**：地址不是大数组，是一张路由表，IO 区读的是硬件；
- **时钟（第 5 集）**：一条事件队列，让所有部件踩着同一个节拍；
- **DMA（第 6 集）**：靠阻塞 CPU 独占总线，高速搬运数据；
- **PPU（第 7 集）**：一行行扫描，画出每一帧；
- **BIOS（第 8 集）**：用 HLE 拦截系统调用，不要真 BIOS 也能跑；
- **声音（第 9 集）**：6 个声道相加，和画面共享同一根时钟；
- **存档（本集）**：因为一切都是变量，整台机器能被冻结、还原。

如果说这一路有什么反复出现的「内核手法」，是这么几个：**查表代替判断**（解码、地址路由）、**事件驱动代替轮询**（时钟调度）、**状态即变量**（寄存器、内存、存档）、**HLE 模拟功能而非过程**（BIOS）。

模拟器内核没有魔法。它只是把一块块硬件的行为，诚实地、一行一行地，翻译成了你我都能读懂的代码。看懂了它，你也就看懂了：所谓「底层」，不过是另一层可以被读懂的抽象而已。

感谢你读到这里。
