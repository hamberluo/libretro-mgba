# 第 2 集《CPU：软件怎么假装成一块 ARM7 芯片》设计

> VitePress 文档站系列第 2 篇。承接序章「一帧的旅程·上」中 CPU 取指-解码-执行的钩子。
> 日期：2026-06-07 ｜ 作者：Hamber

## 1. 定位与承接

- 系列第 2 集，CPU 篇，**第一篇硬核**——真讲代码。
- 承接序章：序章用 `PipelineDemo` 组件演示了「取指→解码→执行」循环，并埋了「解码=查表法」的钩子。本集把这个循环的**真实源码** `ARMStep` 逐行拆开，兑现钩子。
- 调性沿用：深入浅出 · 双层结构（表层比喻 + 里层真实源码）。但里层比重显著高于序章——这一篇核心就是读懂一个函数。

## 2. 内容深度（决策 A）

**聚焦讲透一个函数：`src/arm/arm.c` 的 `ARMStep`（第 201-218 行，已核实）。**

这 11 行代码是整个 CPU 模拟的心脏，包含 CPU 模拟的全部核心要素。不发散到解码器内部实现（decoder-arm.c）或具体指令实现（isa-arm.c）——那些留给后续集（第 3 集「Thumb 指令集实战」已规划）。

### `ARMStep` 真实源码（讲解主体，已核实）

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

### 六个要讲透的要素（文章主干结构）

1. **取指 + 流水线预取**：`opcode = prefetch[0]`（取当前），`prefetch[0] = prefetch[1]`（下一条递补），再 `LOAD_32` 预读新的 `prefetch[1]`。真实硬件的三级流水线在这里被简化为一个 2 槽预取队列——这是「软件假装硬件」的精髓：不必精确模拟每一级流水线，只要行为（PC 领先实际执行两条指令）对得上。
2. **PC 递增**：`gprs[ARM_PC] += WORD_SIZE_ARM`（ARM 模式每条指令 4 字节）。引出寄存器组 `gprs` 就是个数组，PC 只是其中一个「特殊寄存器」。
3. **条件执行（ARM 特色）**：`condition = opcode >> 28`，用 `conditionLut` 查表判断当前标志位是否满足条件。ARM 几乎每条指令都带 4 位条件码——讲清「为什么 ARM 要这样设计」（减少分支）。
4. **解码 = 查表法**（兑现序章钩子）：`_armTable[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0x00F)]`。把 opcode 的特征位拼成一个索引，直接查一张预先建好的函数指针表，O(1) 拿到对应指令的实现。这就是序章说的「查表法」——不用一堆 if-else 判断指令类型。
5. **执行**：`instruction(cpu, opcode)`——通过函数指针调用具体指令实现（实现细节留后续集）。
6. **周期计费**：条件不满足时 `cpu->cycles += ARM_PREFETCH_CYCLES` 直接返回。呼应序章「周期精确」——每条指令、甚至「被跳过的指令」都要计时。

### 锚点行号（GitHub 跳转，已核实）
- `ARMStep`：`src/arm/arm.c#L201`
- `conditionLut`：`src/arm/arm.c#L182`
- `ARMRun`（调度循环，可选提及）：`src/arm/arm.c#L229`

## 3. 交互组件（决策 A）：`ArmStepDemo.vue`

**升级版流水线组件**——序章 `PipelineDemo` 的自然进化。

### 形态
- **左侧**：`ARMStep` 的真实代码（精简到核心几行），单步时**高亮当前执行到的代码行**。
- **右侧**：CPU 状态面板，显示 `PC`、`prefetch[0]`、`prefetch[1]`、`cycles`、关键标志位，单步时**联动更新并高亮变化的字段**。
- **控制**：单步 / 重置（本组件以「单步看状态变化」为核心，不做自动播放——观众需要停下来看每步状态）。
- **场景**：用一个写死的、简化的指令序列（2-3 条假想指令）跑这个循环，让观众看到 PC 如何领先、prefetch 如何递补、cycles 如何累加。**这是教学演示，不是真实 ARM 模拟器**——文中明确标注「示意，非精确仿真」，避免误导。

### 技术
- 纯 Vue3 + CSS，与 `PipelineDemo` 同风格（青绿 `#00d4aa`、暗色）。
- 自包含，无 props。
- 代码行高亮用一个 `activeLine` ref 控制；状态用 reactive 对象，变化字段加 `.changed` class 做高亮动画。
- 响应式：窄屏代码与状态面板上下堆叠。

### 与序章组件的关系
不替换 `PipelineDemo`（序章仍用它）。`ArmStepDemo` 是更深一档的新组件，沉淀为「代码行高亮 + 状态联动」这一交互范式，供后续讲代码的集复用。

## 4. 文章结构（按内容自然铺，决策 C）

不预设句数，以「讲透 `ARMStep` 且不注水」为准。预计为比序章厚实的中等长文。章节：

1. **开篇**：承接序章——「上一集我们说 CPU 在重复取指-解码-执行。这一集，我们看它在 mGBA 里真实的样子。」抛出：原来整个循环就是这一个函数。
2. **先看全貌**：贴出完整 `ARMStep`，告诉读者「11 行，包含 CPU 的全部灵魂」，给 GitHub 跳转。
3. **逐要素拆解**：按 §2 的六个要素分小节，每节配相关代码行 + 比喻 + 为什么这么设计。
4. **交互体验**：嵌入 `<ArmStepDemo />`，让读者单步感受状态流动。
5. **回到全局**：这个函数被谁反复调用（`ARMRun`/调度循环）——呼应序章「时钟/周期精确」，预告第 5 集时间调度。
6. **下集预告**：第 3 集 Thumb 指令集实战（指令真正怎么执行的，即 `instruction(cpu, opcode)` 里面发生什么）。

## 5. 站点改动

- 新增 `website/guide/ep02-cpu.md`。
- 新增 `website/components/ArmStepDemo.vue` + 在 `theme/index.ts` 注册。
- 更新 `website/.vitepress/config.ts` 的 sidebar：加第 2 集条目。
- 更新 `website/index.md` 首页地图：CPU 卡片从「敬请期待」改为「已上线」并加 link。

## 6. base_scene.py 抽取（NOTES 待办）

序章 NOTES 提到「抽 base_scene.py」——那是 manim 视频时代的待办。**现已转向文档站，manim 路线不再继续，此待办作废。** 文档站的复用单元是 Vue 组件（`PipelineDemo` / `ArmStepDemo`），无需 base_scene.py。本集不做该工作，并在序章 NOTES 标注作废。

## 7. 不做什么（YAGNI）

- 不深入 decoder-arm.c 的解码实现、不讲具体指令在 isa-arm.c 的实现（后续集）。
- `ArmStepDemo` 不做真实 ARM 仿真，只做教学示意（明确标注）。
- 不做自动播放（单步为主）。
- 不预设/强凑句数。

## 8. 验收标准

1. `ep02-cpu.md` 讲透 `ARMStep` 六要素，含真实源码块 + GitHub 跳转（arm.c#L201 等）。
2. `<ArmStepDemo />` 可单步，代码行高亮 + 右侧状态（PC/prefetch/cycles）联动更新，标注「教学示意」。
3. 首页地图 CPU 卡片可点进；sidebar 有第 2 集。
4. `npm run docs:build` 构建通过、无死链；push 后线上可访问 `/guide/ep02-cpu`。
5. 双层结构：每个要素都有「比喻/为什么」表层 + 真实代码里层。

## 9. 下一步

本设计通过 → writing-plans 出实现计划（组件 → 正文 → config/首页接线 → 构建验证 → 部署）。
