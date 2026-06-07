# 第 3 集《一条指令的执行 · Thumb 指令集实战》设计

> VitePress 文档站系列第 3 篇。承接 CPU 篇结尾的钩子：`instruction(cpu, opcode)` 里到底发生什么、周期从哪来。
> 日期：2026-06-07 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 定位与承接

- 系列第 3 集。CPU 篇讲到「解码查表拿到一个函数指针，调用它就执行指令」，并把「具体指令怎么实现、周期从哪来」留给本集。本集走进那个被调用的函数。
- 选 **Thumb 指令集**（而非 ARM）：GBA 游戏绝大多数代码跑在 Thumb 模式（16 位指令、代码密度高），承接自然，`isa-thumb.c`（417 行）也比 isa-arm.c 精简易讲。
- 调性沿用：深入浅出 · 双层结构。里层比重高（讲宏展开的真实代码）。

## 2. 内容深度

**聚焦两点：① `DEFINE_INSTRUCTION_THUMB` 宏的结构；② 以 `ADD3` 为主角指令走完一条加法的执行。**

不铺开讲所有指令类型（移位、访存、分支等），只用 ADD 把「解码操作数 → 执行运算 → 更新标志位 → 计费」这条链讲透，其余指令点到为止。

### 核心源码（已核实，来自 `src/arm/isa-thumb.c`）

**指令包装宏（L58）——周期计费的真身：**
```c
#define DEFINE_INSTRUCTION_THUMB(NAME, BODY) \
	static void _ThumbInstruction ## NAME (struct ARMCore* cpu, unsigned opcode) {  \
		int currentCycles = THUMB_PREFETCH_CYCLES; \
		BODY; \
		cpu->cycles += currentCycles; \
	}
```

**操作数解码宏（L112，data form 1）：**
```c
#define DEFINE_DATA_FORM_1_INSTRUCTION_THUMB(NAME, BODY) \
	DEFINE_INSTRUCTION_THUMB(NAME, \
		int rm = (opcode >> 6) & 0x0007; \
		int rd = opcode & 0x0007; \
		int rn = (opcode >> 3) & 0x0007; \
		BODY;)
```

**ADD3 指令（L119）——主角：**
```c
DEFINE_DATA_FORM_1_INSTRUCTION_THUMB(ADD3, THUMB_ADDITION(cpu->gprs[rd], cpu->gprs[rn], cpu->gprs[rm]))
```

**加法 + 标志位宏（L13、L38）：**
```c
#define THUMB_ADDITION_S(M, N, D) \
	cpu->cpsr.flags = 0; \
	cpu->cpsr.n = ARM_SIGN(D); \
	cpu->cpsr.z = !(D); \
	cpu->cpsr.c = ARM_CARRY_FROM(M, N, D); \
	cpu->cpsr.v = ARM_V_ADDITION(M, N, D);

#define THUMB_ADDITION(D, M, N) \
	int n = N; \
	int m = M; \
	D = M + N; \
	THUMB_ADDITION_S(m, n, D)
```

### 要讲透的链条（文章主干）
1. **宏即模板**：每条 Thumb 指令实现都被 `DEFINE_INSTRUCTION_THUMB` 包成一个 `static void _ThumbInstructionXXX(cpu, opcode)`——正好是 CPU 篇 `_thumbTable` 里存的那种函数指针。预处理器是这套代码的「代码生成器」。
2. **周期从哪来**（兑现 CPU 篇钩子）：宏开头 `currentCycles = THUMB_PREFETCH_CYCLES`、结尾 `cpu->cycles += currentCycles`。**每条指令自己负责记自己的账**——这就是 CPU 篇 ARMStep 里没看到周期累加的原因：账记在指令实现里。访存类指令还会把额外周期写进 `currentCycles`（为第 4 集内存篇埋钩子）。
3. **操作数解码**：`ADD3` 用的 data form 1 宏从 16 位 opcode 里抠出三个 3 位字段 `rd/rn/rm`（寄存器编号 0-7）。承接 CPU 篇「PC 只是数组下标」——这里 rd/rn/rm 也都是 `gprs[]` 的下标。
4. **执行运算**：`THUMB_ADDITION` 宏做 `D = M + N`（即 `gprs[rd] = gprs[rn] + gprs[rm]`）。
5. **更新标志位**：`THUMB_ADDITION_S` 设置 N（负）、Z（零）、C（进位）、V（溢出）——这四个标志位正是 CPU 篇第四节「条件执行」查 `conditionLut` 时用的 `cpsr.flags`。闭环：一条指令算完设标志，下一条带条件的指令据此决定执行与否。

### 锚点行号（GitHub 跳转，已核实）
- `DEFINE_INSTRUCTION_THUMB`：`src/arm/isa-thumb.c#L58`
- `DEFINE_DATA_FORM_1_INSTRUCTION_THUMB`：`src/arm/isa-thumb.c#L112`
- `ADD3`：`src/arm/isa-thumb.c#L119`
- `THUMB_ADDITION_S`：`src/arm/isa-thumb.c#L13`

## 3. 交互组件：`ThumbAddDemo.vue`

复用 ArmStepDemo 的「代码行高亮 + 状态联动」范式，深入到「一条指令内部」。

### 形态
- **顶部输入**：两个可调寄存器值 `rn`、`rm`（给几个预设按钮，如「正常加」「触发进位」「触发溢出」，避免自由输入的复杂度）。
- **左侧**：ADD3 执行的简化代码行（解码 rd/rn/rm → 相加 → 设标志），单步高亮。
- **右侧**：状态面板显示 `gprs[rd]` 结果 + 四个标志位 N/Z/C/V，变化高亮。重点让观众看到「同样一条 ADD，输入不同，标志位不同」。
- **控制**：选预设 / 单步 / 重置。
- 标注「教学示意」，纯 Vue3+CSS，青绿暗色风格。

### 与已有组件关系
不替换 ArmStepDemo。三个组件递进：PipelineDemo（循环直觉）→ ArmStepDemo（取指循环真身）→ ThumbAddDemo（一条指令内部）。沉淀「输入→单步→看状态/标志」范式。

## 4. 文章结构（按内容自然铺）

1. 开篇：承接 CPU 篇——「上集解码的终点是调用一个函数。这集我们打开这个函数。」
2. 宏即模板：`DEFINE_INSTRUCTION_THUMB` 怎么把一行 BODY 变成一个完整指令函数；预处理器是代码生成器。
3. 周期从哪来：兑现 CPU 篇钩子，currentCycles 一头一尾。
4. 一条 ADD 的全过程：解码操作数 → 相加 → 设标志位，逐宏展开。
5. 标志位闭环：N/Z/C/V 如何回喂给 CPU 篇的条件执行。
6. 交互：嵌 `<ThumbAddDemo />`，调预设看标志位变化。
7. 下集预告：访存指令把周期写进 currentCycles（`load32(..., &currentCycles)`）——一次内存访问到底花几个周期？引出第 4 集《内存不是数组：MMIO 与地址映射》。

## 5. 站点改动

- 新增 `website/guide/ep03-thumb.md`。
- 新增 `website/components/ThumbAddDemo.vue` + theme/index.ts 注册（保留 PipelineDemo、ArmStepDemo）。
- config.ts sidebar 加第 3 集。
- index.md 首页地图第 3 集卡片转「已上线」+ link。

## 6. 不做什么（YAGNI）

- 不讲移位/访存/分支/SWI 等其它 Thumb 指令类型（只用 ADD 贯穿，其余点到为止）。
- 不讲 ARM 模式指令（isa-arm.c）。
- ThumbAddDemo 用预设输入，不做任意寄存器自由编辑。
- 不预设句数。

## 7. 验收标准

1. `ep03-thumb.md` 讲透宏模板 + ADD3 全过程 + 标志位闭环，含真实源码块 + 4 个 GitHub 跳转（isa-thumb.c#L58/L112/L119/L13）。
2. `<ThumbAddDemo />` 可选预设、单步，结果 + N/Z/C/V 标志位联动，标注教学示意。
3. 首页第 3 集卡片可点进；sidebar 有第 3 集。
4. build 通过无死链；线上可访问 `/guide/ep03-thumb`。
5. 双层结构 + 承接 CPU 篇钩子 + 为第 4 集埋钩子。

## 8. 下一步

writing-plans 出实现计划（组件 → 正文 → 接线 → 部署），subagent 执行，部署上线后向 Hamber 简短汇报。
