# 第 10 集《随时存档读档：把整台机器冻在一瞬间》设计（系列收官）

> VitePress 文档站系列第 10 篇，收官。承接第 9 集，闭环序章的「整台机器是一个结构体」。
> 日期：2026-06-08 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 定位与承接

- 系列最后一集。前 9 集把一台 GBA 怎么活过来讲全了。本集讲模拟器独有的超能力：把正在运行的机器精确「冻」在一瞬间，之后分毫不差还原。打开 `serialize.c`。
- **闭环序章**：序章第一集就讲过「在 mGBA 源码里，整台机器就是一个结构体 `struct GBA`」。本集正是这句话的终点——因为整台机器就是一堆 C 变量，所以它能被一次性存下来、读回来。
- 收官篇正文末尾做整个系列的总回顾。
- 调性沿用：深入浅出 · 双层结构。

## 2. 内容深度

**聚焦讲透 `src/gba/serialize.c` 的 `GBASerialize`（L27）：把整台机器的状态逐字段打成快照。** 不逐字段讲完所有子系统的序列化、不深入兼容性迁移的全部细节——讲「快照=抄变量」「魔数防坑」「能冻结的根因」这一层，并借存的字段回顾全系列。

### 核心源码（已核实，节选）

```c
void GBASerialize(struct GBA* gba, struct GBASerializedState* state) {
	STORE_32(GBASavestateMagic + GBASavestateVersion, 0, &state->versionMagic);  // 魔数+版本
	STORE_32(gba->biosChecksum, 0, &state->biosChecksum);
	STORE_32(gba->romCrc32, 0, &state->romCrc32);
	STORE_32(gba->timing.masterCycles, 0, &state->masterCycles);   // 第5集：主时钟

	int i;
	for (i = 0; i < 16; ++i) {
		STORE_32(gba->cpu->gprs[i], i * sizeof(state->cpu.gprs[0]), state->cpu.gprs);  // 第2集：寄存器组
	}
	STORE_32(gba->cpu->cpsr.packed, 0, &state->cpu.cpsr.packed);   // 第3集：标志位
	STORE_32(gba->cpu->cycles, 0, &state->cpu.cycles);
	STORE_32(gba->cpu->nextEvent, 0, &state->cpu.nextEvent);       // 第5集：下一个事件
	STORE_32(gba->cpu->prefetch[0], 0, state->cpuPrefetch);        // 第2集：流水线预取
	STORE_32(gba->cpu->prefetch[1], 4, state->cpuPrefetch);
	// ... 接着是 memory / video / audio / dma / timers 各子系统状态
}
```

（读档是它的镜像：`GBADeserialize`（L96）把每个字段 `LOAD_32` 还原回部件。）

### 要讲透的三点（文章主干）
1. **存档 = 把所有部件的状态抄一份**。`GBASerialize` 做的事极其直白：把 CPU 的 16 个寄存器、cpsr 标志位、cycles、nextEvent、流水线预取、主时钟 masterCycles，加上 memory/video/audio/dma 各子系统的当前值，逐个 `STORE_32` 写进一个大结构体 `GBASerializedState`。**这个结构体，就是某一瞬间整台机器的完整快照。**
2. **为什么能「冻结」——因为状态就是一堆变量**（闭环全系列）。回顾前 9 集你会发现：CPU 的寄存器是数组（第 2 集）、标志位是 cpsr 的位（第 3 集）、内存是各区域的字节（第 4 集）、时钟是 masterCycles（第 5 集）、PPU 进度是 vcount（第 7 集）、音频/DMA 状态也都是结构体字段。**整台机器没有任何「藏在硬件里看不见」的状态——全是 C 变量。** 所以「冻结」只需把这些变量抄一份，「还原」只需抄回去。这是真机做不到、模拟器独有的超能力。
3. **versionMagic：序列化的经典坑**。第一个字段就是 `GBASavestateMagic + GBASavestateVersion`——魔数（标识「这是个 mGBA 存档」）+ 版本号。读档时先验证：魔数不对说明不是有效存档，版本不对说明结构变了（新版本加了字段、改了布局），直接拒绝或走兼容路径。**否则用旧档去填新结构，字段错位，还原出一台「精神错乱」的机器。** 这是所有「保存/加载结构化状态」都要面对的坑——存档格式必须自带版本。

### 锚点行号（GitHub 跳转，已核实）
- `GBASerialize`：`src/gba/serialize.c#L27`
- `GBADeserialize`：`src/gba/serialize.c#L96`

## 3. 交互组件：`SaveStateDemo.vue`

存档/读档可视化。

### 形态
- **左侧「运行中的机器」**：几个关键部件状态（PC、某寄存器 r0、masterCycles 时钟、PPU 当前行 vcount、音频采样数），数值在「运行」时跳动（或可点「运行一下」让它们变化）。
- **中间快照区**：点「存档」→ 把当前各状态值复制进一个「快照」结构体框（显示存下来的值，加版本号 magic）。
- **流程**：① 存档（快照拍下当前值）→ ② 点「继续运行」让左侧状态变化（和快照不同了）→ ③ 点「读档」→ 左侧各状态被快照值精确覆盖还原（高亮变回快照值）。
- 控制：运行一下 / 存档 / 读档 / 重置。
- 说明：强调「读档=把快照值抄回每个部件」，和真机存档（只能存游戏进度）对比——模拟器能存「整台机器」。
- 纯 Vue3+CSS，青绿暗色，标注教学示意。
- **遵循统一规范**：active/变化高亮 0.14、按钮 hover 0.12、选中按钮 `.on:hover {#4af0d2}`、数字 nowrap。快照区可用 GOLD 强调。

### 与已有组件关系
第十个、也是最后一个交互组件。表现「快照→改变→还原」，呼应整台机器状态可被完整捕获。

## 4. 文章结构（自然铺，收官篇可略丰）

1. 开篇：前 9 集机器活过来了；模拟器还有个真机没有的超能力——随时冻结、精确还原。打开 serialize.c。
2. 存档就是抄变量：GBASerialize 逐字段 STORE_32 进快照结构体。
3. 为什么能冻结：因为状态全是 C 变量（借存的字段回顾第 2/3/4/5/7 集）。
4. versionMagic 防坑：魔数+版本，读档先验证，否则字段错位。
5. 交互：嵌 `<SaveStateDemo />`，存档→运行→读档看精确还原。
6. **系列总收束**：用一段把 10 集串成一条完整认知地图——序章的「一帧的诞生」主线（CPU→指令→内存→时钟→DMA→PPU→声音）+ 两块拼图（BIOS、存档）。点出贯穿全系列的几个核心手法（查表、事件驱动、状态即变量、HLE）。收尾语：模拟器内核没有魔法，它只是把硬件的行为，诚实地翻译成了一行行能读懂的代码。

## 5. 站点改动

- 新增 `website/guide/ep10-savestate.md`。
- 新增 `website/components/SaveStateDemo.vue` + theme/index.ts 注册（保留前九个）。
- config.ts sidebar 加第 10 集。
- index.md 首页地图第 10 集卡片转「已上线」+ link（至此 10 集全部上线，无占位卡片）。

## 6. 不做什么（YAGNI）

- 不逐字段讲完所有子系统序列化、不讲 GBADeserialize 的全部校验分支。
- 不讲存档兼容性迁移（旧版本字段补全）的具体实现。
- 不讲 savedata（游戏内存档）与 savestate（模拟器状态快照）的区别细节，只点一句对比。
- SaveStateDemo 用几个示意状态值，不做真实序列化。
- 不预设句数。

## 7. 验收标准

1. `ep10-savestate.md` 讲透 GBASerialize 快照 + 状态即变量（回顾全系列）+ versionMagic 防坑 + 系列总收束，含真实源码块 + 2 个 GitHub 跳转（serialize.c#L27、serialize.c#L96）。
2. `<SaveStateDemo />` 可运行/存档/读档、读档精确还原快照值，遵循统一规范；无文本溢出。
3. 首页第 10 集卡片可点进；sidebar 有第 10 集；**首页 10 集地图全部为已上线状态（无【敬请期待】）**。
4. build 通过无死链；线上可访问 `/guide/ep10-savestate`。
5. 双层结构 + 闭环序章「整台机器是一个结构体」+ 系列总收束。

## 8. 下一步

writing-plans → subagent 执行 → 部署 → headless 截图自查组件无溢出 + **检查首页无残留【敬请期待】** → 向 Hamber 做整个系列的完整收官汇报。
