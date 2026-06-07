# 第 6 集《DMA：不打扰 CPU 的搬运工》设计

> VitePress 文档站系列第 6 篇。承接第 5 集结尾的钩子：DMA 凭什么「抢」过 CPU 占用总线。
> 日期：2026-06-07 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 定位与承接

- 系列第 6 集。第 5 集事件队列里出现了「DMA 传输」这类事件，并留钩子：DMA 是「不打扰 CPU 的搬运工」，它凭什么能抢过 CPU 占用总线？本集打开 `GBADMAService` 回答。
- 三集知识在此汇合：第 4 集的 load/store 与 waitstate、第 5 集的事件 when、本集的总线阻塞。
- 调性沿用：深入浅出 · 双层结构。

## 2. 内容深度

**聚焦讲透 `src/gba/dma.c` 的 `GBADMAService`（L263）：① 抢总线的真相（阻塞 CPU）；② 搬运本体（load→store→步进）；③ 周期占用。** 不铺开 4 条 DMA 通道的优先级、HBlank/VBlank/FIFO 各触发时机的全部细节——用一次传输把核心讲透。

### 核心源码（已核实，节选自 GBADMAService）

```c
void GBADMAService(struct GBA* gba, int number, struct GBADMA* info) {
	struct GBAMemory* memory = &gba->memory;
	struct ARMCore* cpu = gba->cpu;
	uint32_t width = 2 << GBADMARegisterGetWidth(info->reg);
	uint32_t source = info->nextSource;
	uint32_t dest = info->nextDest;
	int32_t cycles = 2;

	gba->cpuBlocked = true;                        // 关键：直接阻塞 CPU
	gba->performingDMA = 1 | (number << 1);
	cpu->memory.accessSource = mACCESS_DMA;

	// ... 按区域 waitstate 累加 cycles
	info->when += cycles;                          // 占用的周期记进时间线

	if (width == 4) {
		info->latch = cpu->memory.load32(cpu, source, 0);   // 从源读
		cpu->memory.store32(cpu, dest, info->latch, 0);     // 写到目标
	} else {
		// ... load16 / store16
	}

	info->nextSource += info->sourceOffset;        // 源地址步进
	info->nextDest += info->destOffset;            // 目标地址步进
	// ...
}
```

### 要讲透的三点（文章主干）
1. **抢总线的真相：直接阻塞 CPU**。`gba->cpuBlocked = true`（L274）—— DMA 并不是「和 CPU 并行、互不干扰」，而是**把 CPU 挂起、自己独占内存总线**。`cpu->memory.accessSource = mACCESS_DMA` 标明此刻总线归 DMA。搬完再放行 CPU。所谓「不打扰」，准确说是「短暂霸占、但很快还回去」。
2. **搬运本体：就是 load + store + 步进**。`load32(source)` 读一个字，`store32(dest, latch)` 写到目标——**正是第 4 集那对 load/store**。然后 `nextSource += sourceOffset`、`nextDest += destOffset`，地址按配置步进（递增/递减/固定）。一次搬一个单位（16 或 32 位），重复直到搬完 count 个。DMA 没什么魔法，它就是一个不需要 CPU 逐条指令驱动的「自动 load-store 循环」。
3. **周期占用：DMA 也要记时间账**。`cycles` 按源/目标区域的 `waitstatesNonseq[region]` 累加（**又是第 4 集的 waitstate**），`info->when += cycles`（**又是第 5 集的事件 when**）。DMA 本身就是登记在第 5 集那条事件队列里的一个事件——它占的总线周期，精确记在时间线上。
4. **「不打扰」的真相辨析**（本集亮点）：DMA 期间 CPU 确实停了。但它「显得不打扰」有两个原因：① 比 CPU 用普通指令一个字一个字搬快得多（专用硬件路径）；② 常被安排在 HBlank/VBlank 这种 CPU 本就在等画面的空隙触发。所以宏观上像「后台搬运」，本质是「高效且挑时机的短暂霸占」。

### 锚点行号（GitHub 跳转，已核实）
- `GBADMAService`：`src/gba/dma.c#L263`

## 3. 交互组件：`DmaTransferDemo.vue`

一次 DMA 传输可视化。

### 形态
- **三栏**：左「源（source）」一列数据格、中间「CPU 状态」（运行中 / 已阻塞）、右「目标（dest）」空格子。
- **单步**：每步把源的一个字搬到目标对应格（高亮当前搬的字），源/目标地址指针步进，CPU 状态显示「⛔ 已阻塞（总线被 DMA 占用）」，底部周期数累加。
- 搬完后 CPU 状态恢复「▶ 运行中」，提示「总线已交还 CPU」。
- 控制：单步 / 重置。
- 纯 Vue3+CSS，青绿暗色，标注教学示意。
- **遵循统一规范**：当前搬运格/active 高亮背景 0.14、按钮 hover 0.12、选中按钮（如有）`.on:hover {#4af0d2}`、数字 nowrap。CPU「已阻塞」用警示色 `#ff6b6b` 区分。

### 与已有组件关系
第六个交互组件。前五个是循环/查表/路由/事件队列，这个是「数据搬运 + CPU 阻塞」可视化，最适合表现 DMA「占总线」的概念。

## 4. 文章结构（自然铺）

1. 开篇：第 5 集队列里那个「DMA 传输」事件——它凭什么抢总线？打开 GBADMAService。
2. 抢总线的真相：cpuBlocked=true，挂起 CPU 独占总线。
3. 搬运本体：load→store→步进，复用第 4 集 load/store，没有魔法。
4. 周期账：waitstate + when，DMA 也是事件，占的周期记时间线（汇合第 4、5 集）。
5. 「不打扰」辨析：短暂霸占 + 挑时机（HBlank/VBlank）+ 比 CPU 快。
6. 交互：嵌 `<DmaTransferDemo />`，单步看搬运 + CPU 阻塞。
7. 下集预告：DMA 常把数据搬进 VRAM（显存）。数据进了显存，怎么变成屏幕上一行行的像素？引出第 7 集《PPU：扫描线是怎么画出来的》。

## 5. 站点改动

- 新增 `website/guide/ep06-dma.md`。
- 新增 `website/components/DmaTransferDemo.vue` + theme/index.ts 注册（保留前五个）。
- config.ts sidebar 加第 6 集。
- index.md 首页地图第 6 集卡片转「已上线」+ link。

## 6. 不做什么（YAGNI）

- 不讲 4 条 DMA 通道的优先级仲裁、各通道触发时机（immediate/HBlank/VBlank/FIFO/视频捕获）的全部细节。
- 不讲 EEPROM/savedata 特殊路径（GBADMAService 里那段 savedata 分支跳过）。
- DmaTransferDemo 用写死的短数据序列，不做任意配置。
- 不预设句数。

## 7. 验收标准

1. `ep06-dma.md` 讲透 cpuBlocked 抢总线 + load/store 搬运 + 周期账 + 「不打扰」辨析，含真实源码块 + GitHub 跳转（dma.c#L263）。
2. `<DmaTransferDemo />` 可单步搬运、CPU 显示阻塞/恢复、地址步进、周期累加，遵循统一规范；无文本溢出。
3. 首页第 6 集卡片可点进；sidebar 有第 6 集。
4. build 通过无死链；线上可访问 `/guide/ep06-dma`。
5. 双层结构 + 承接第 5 集 DMA 钩子 + 汇合第 4/5 集 + 为第 7 集 PPU 埋钩子。

## 8. 下一步

writing-plans → subagent 执行 → 部署 → headless 截图自查组件无溢出/规范一致 → 向 Hamber 简短汇报。
