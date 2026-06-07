# 第 4 集《内存不是数组：MMIO 与地址映射》设计

> VitePress 文档站系列第 4 篇。承接第 3 集结尾的钩子：一次内存访问几个周期、内存为什么不是大数组。
> 日期：2026-06-07 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 定位与承接

- 系列第 4 集。第 3 集讲访存指令把额外周期写进 `currentCycles`（`load32(..., &currentCycles)`），并留了钩子：一次内存访问到底几个周期、内存为什么不是简单大数组。本集打开 `GBALoad32` 回答。
- 调性沿用：深入浅出 · 双层结构。里层讲真实地址路由代码。

## 2. 内容深度

**聚焦讲透 `GBALoad32`（src/gba/memory.c#L476）的两件事：① 地址映射（switch 路由）；② 周期来源（waitstate）。**

不铺开 load8/load16/store 全家族、不深入各区域读取宏（LOAD_VRAM 等）的内部——用 load32 一条把「地址→区域→周期」讲透。

### 核心源码（已核实）

**地址路由（memory.c#L476，switch 主体）：**
```c
uint32_t GBALoad32(struct ARMCore* cpu, uint32_t address, int* cycleCounter) {
	struct GBA* gba = (struct GBA*) cpu->master;
	struct GBAMemory* memory = &gba->memory;
	uint32_t value = 0;
	int wait = 0;
	char* waitstatesRegion = memory->waitstatesNonseq32;

	switch (address >> BASE_OFFSET) {
	case GBA_REGION_BIOS:        LOAD_BIOS;        break;
	case GBA_REGION_EWRAM:       LOAD_EWRAM;       break;
	case GBA_REGION_IWRAM:       LOAD_IWRAM;       break;
	case GBA_REGION_IO:          LOAD_IO;          break;
	case GBA_REGION_PALETTE_RAM: LOAD_PALETTE_RAM; break;
	case GBA_REGION_VRAM:        LOAD_VRAM;        break;
	case GBA_REGION_OAM:         LOAD_OAM;         break;
	case GBA_REGION_ROM0: /* ... */ LOAD_CART;     break;
	case GBA_REGION_SRAM: /* ... */ LOAD_SRAM;     break;
	default:                     LOAD_BAD;         break;
	}

	if (cycleCounter) {
		wait += 2;
		if (address < GBA_BASE_ROM0) {
			wait = GBAMemoryStall(cpu, wait);
		}
		*cycleCounter += wait;
	}
	// ...
}
```

**区域常量（include/mgba/internal/gba/memory.h#L24，BASE_OFFSET=24）：**
GBA_REGION_BIOS=0x0、EWRAM=0x2、IWRAM=0x3、IO=0x4、PALETTE_RAM=0x5、VRAM=0x6、OAM=0x7、ROM0=0x8、SRAM=0xE。

### 要讲透的两点（文章主干）
1. **内存不是大数组，是一张路由表**：`address >> BASE_OFFSET`（BASE_OFFSET=24）取地址最高字节当「区域号」，switch 分发到 9 类存储。每类背后是独立的存储和独立的读取逻辑（宏 `LOAD_xxx`）。一个 32 位地址，高 8 位决定「你在跟哪块硬件说话」。
2. **MMIO：IO 区域读的不是内存**：`GBA_REGION_IO`（0x4 段）走 `LOAD_IO`——读到的是 PPU/定时器/DMA/按键等硬件寄存器的当前值，不是某块 RAM。这就是 Memory-Mapped IO：用读内存的方式读硬件状态。承接序章「按键被记录到内存一个固定位置」——那个位置就在 IO 区。
3. **周期从哪来**（兑现第 3 集钩子）：`*cycleCounter += wait`。wait 来自 `waitstatesNonseq32[region]`——每个区域访问快慢不同（IWRAM 最快、EWRAM 慢、ROM 取决于卡带 waitstate 配置）。还有 `GBAMemoryStall`（预取单元争用总线的额外停顿）。这正是第 3 集那个 cycleCounter 的来源：访存的周期账，记在这里。

### 锚点行号（GitHub 跳转，已核实）
- `GBALoad32`：`src/gba/memory.c#L476`
- 区域枚举：`include/mgba/internal/gba/memory.h#L24`

## 3. 交互组件：`MemoryMapDemo.vue`

地址路由可视化——比前几个组件更适合内存主题。

### 形态
- **顶部**：一个地址输入（给若干预设按钮：BIOS地址 0x00000000 / EWRAM 0x02000000 / IWRAM 0x03000000 / IO 0x04000000 / VRAM 0x06000000 / ROM 0x08000000 / SRAM 0x0E000000）。
- **中间**：一张竖排的 GBA 内存地图（9 个区域块，按地址从低到高），当前地址命中的区域块高亮。
- **底部信息**：显示 `address >> 24 = 区域号 → 区域名`，以及该区域的相对访问速度（定性：IWRAM 最快 / EWRAM 较慢 / ROM 看 waitstate / IO 是寄存器非 RAM）。
- 纯 Vue3+CSS，青绿暗色，标注「教学示意，地址范围简化」。
- **布局注意**（吸取第 2 集教训）：地址/区域名等 hex 文本要留够宽、nowrap，避免溢出；状态信息区用足够 flex 宽度。

### 与已有组件关系
第四个交互组件，递进：PipelineDemo（循环）→ ArmStepDemo（取指循环）→ ThumbAddDemo（一条指令内部）→ MemoryMapDemo（地址路由）。沉淀「输入→看路由/映射结果」范式。

## 4. 文章结构（自然铺）

1. 开篇：承接第 3 集——访存指令花的那些周期是怎么来的？打开 GBALoad32。
2. 内存不是大数组：switch (address >> 24) 路由，9 个区域。
3. MMIO：IO 区读的是寄存器不是 RAM；呼应序章按键。
4. 周期从哪来：waitstate + GBAMemoryStall，兑现第 3 集钩子。
5. 交互：嵌 `<MemoryMapDemo />`，输入地址看落到哪个区域、多快。
6. 下集预告：内存里有一块特殊区域 VRAM（显存），数据进了显存怎么变成画面？而且有个「不打扰 CPU 的搬运工」专门往那儿搬数据——引出第 5 集（按大纲第 5 集是时钟/调度，第 6 集 DMA）。注：实际承接到时钟调度（第 5 集），用「CPU 跑够周期就停下来交给别人」收束到时间主宰。

## 5. 站点改动

- 新增 `website/guide/ep04-memory.md`。
- 新增 `website/components/MemoryMapDemo.vue` + theme/index.ts 注册（保留前三个组件）。
- config.ts sidebar 加第 4 集。
- index.md 首页地图第 4 集卡片转「已上线」+ link。

## 6. 不做什么（YAGNI）

- 不讲 load8/load16/store 全家族（只用 load32）。
- 不深入各区域 LOAD_xxx 宏内部实现。
- 不讲 waitstate 的精确数值表（只定性讲快慢差异）。
- MemoryMapDemo 用预设地址，地址范围简化展示，不做任意地址解析的完整边界。
- 不预设句数。

## 7. 验收标准

1. `ep04-memory.md` 讲透地址路由 + MMIO + 周期来源，含真实源码块 + 2 个 GitHub 跳转（memory.c#L476、memory.h#L24）。
2. `<MemoryMapDemo />` 可选预设地址、高亮命中区域、显示区域号/名/速度，标注教学示意；无文本溢出。
3. 首页第 4 集卡片可点进；sidebar 有第 4 集。
4. build 通过无死链；线上可访问 `/guide/ep04-memory`。
5. 双层结构 + 承接第 3 集周期钩子 + 为第 5 集（时钟调度）收束。

## 8. 下一步

writing-plans 出实现计划 → subagent 执行 → 部署上线 → **headless 截图自查组件无溢出**（吸取第 2 集教训）→ 向 Hamber 简短汇报。
