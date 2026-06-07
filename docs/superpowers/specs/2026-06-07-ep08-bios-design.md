# 第 8 集《没有真 BIOS，游戏怎么还能跑？》设计

> VitePress 文档站系列第 8 篇。承接第 7 集结尾的钩子：游戏一开机连 BIOS 都没有，怎么跑起来。
> 日期：2026-06-07 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 定位与承接

- 系列第 8 集。第 7 集走完了「一帧的诞生」主线，留了块拼图：很多模拟器不带真 BIOS ROM，游戏照样跑——怎么做到的？本集打开 `bios.c` 的 `GBASwi16` 回答。
- 本集立一个核心概念：**HLE（高级模拟）vs LLE（低级模拟）**。
- 调性沿用：深入浅出 · 双层结构。

## 2. 内容深度

**聚焦讲透 `src/gba/bios.c` 的 `GBASwi16`（L408）：游戏通过 SWI 请求 BIOS 服务，mGBA 拦截后用 C 直接实现，而非跑真 BIOS。** 不逐个讲所有 SWI 调用的实现细节（_unLz77 解压、_ArcTan 三角函数等），用 Div（除法）做主例。

### 核心源码（已核实，节选）

```c
void GBASwi16(struct ARMCore* cpu, int immediate) {
	struct GBA* gba = (struct GBA*) cpu->master;
	// ...
	if (gba->memory.fullBios) {       // 如果挂载了真 BIOS
		ARMRaiseSWI(cpu);             // 走真路径：跳进真 BIOS 的 ARM 代码
		return;
	}

	switch (immediate) {              // 否则 HLE：按 SWI 编号，用 C 直接实现
	case GBA_SWI_DIV:
		_Div(gba, cpu->gprs[0], cpu->gprs[1]);   // 除法，结果写回寄存器
		break;
	case GBA_SWI_SQRT:
		cpu->gprs[0] = _Sqrt(cpu->gprs[0], &gba->biosStall);
		break;
	case GBA_SWI_CPU_SET:
	case GBA_SWI_CPU_FAST_SET:
		// 内存拷贝...
		break;
	// ... 还有 ArcTan、LZ77 解压、各种系统例程
	}
}
```

### 要讲透的三点（文章主干）
1. **游戏怎么"请求"BIOS：SWI 软件中断**。游戏不会直接跳到 BIOS 里某个地址，而是执行一条 `SWI 编号` 指令（第 3 集 Thumb 提过 `cpu->irqh.swi16`）。「编号」说明它要哪个服务：0x06 是除法、0x08 是平方根、0x0B 是 CpuSet（内存拷贝）……这是一套约定的「系统调用表」。
2. **HLE：拦截 SWI，用 C 直接给结果**。`GBASwi16` 的 `switch (immediate)` 拦下这个编号，**不去跑真 BIOS 那几十条 ARM 指令**，而是直接调一个 C 函数算出结果、写回寄存器（`_Div` 把商写 r0、余数写 r1）。游戏拿到的结果一模一样，但模拟器只跑了一个 C 函数。这就是 **HLE（High-Level Emulation，高级模拟）：模拟「功能/结果」，不模拟「过程」。**
3. **两条路并存，所以不需要真 BIOS**。`if (gba->memory.fullBios)` ——用户若提供了真 BIOS ROM，就 `ARMRaiseSWI` 走真路径（逐指令跑真 BIOS，即 **LLE 低级模拟**）；没有，就走上面的 HLE。HLE 这条路让模拟器**不依赖受版权保护的 BIOS ROM** 也能跑游戏——这就是「没有真 BIOS 还能跑」的答案。（`hle-bios.c` 里那个字节数组，是一段最小 ARM 桩，补上中断向量等少数没法纯 C 替代的部分。）

### HLE vs LLE（本集核心概念，单列一节）
- **LLE（低级模拟）**：忠实跑真硬件的代码/逻辑，逐指令。精确度最高，但慢、且需要真 BIOS ROM。
- **HLE（高级模拟）**：识别「要做什么」，用宿主代码直接实现结果。快、不需 ROM，但可能与真硬件有细微差异（边角行为、时序）。
- mGBA 两者都支持：有真 BIOS 走 LLE，没有走 HLE。这是模拟器设计的经典权衡。

### 锚点行号（GitHub 跳转，已核实）
- `GBASwi16`：`src/gba/bios.c#L408`

## 3. 交互组件：`SwiCallDemo.vue`

一次 SWI（以 Div 为例）调用可视化。

### 形态
- **场景**：游戏要算 `10 / 3`。给两个输入（被除数 r0、除数 r1，用几个预设：10÷3、100÷7、20÷4）。
- **流程单步**：① 游戏执行 `swi 0x06`（请求除法）→ ② mGBA 拦截，判断「无真 BIOS → 走 HLE」→ ③ 调 C 函数 `_Div`，直接算出商和余 → ④ 结果写回 r0（商）、r1（余）。每步高亮、显示寄存器变化。
- **对比标注**：HLE「1 个 C 函数搞定」 vs LLE「要跑真 BIOS 里几十条 ARM 指令」。
- 控制：选预设 / 单步 / 重置。
- 纯 Vue3+CSS，青绿暗色，标注教学示意。
- **遵循统一规范**：active 高亮 0.14、按钮 hover 0.12、选中按钮 `.on:hover {#4af0d2}`、数字 nowrap。

### 与已有组件关系
第八个交互组件。表现「拦截→直接给结果」这个 HLE 的核心动作。

## 4. 文章结构（自然铺）

1. 开篇：第 7 集走完主线，但很多模拟器不带 BIOS ROM 也能跑游戏——怎么回事？
2. 游戏怎么请求 BIOS：SWI 软件中断 + 编号（系统调用表）。
3. HLE：GBASwi16 拦截编号，用 C 直接给结果（Div 为例），不跑真 BIOS。
4. 两条路：fullBios 走 LLE（真 BIOS 逐指令），否则 HLE——所以不需要 ROM。
5. HLE vs LLE：经典权衡（快/不需ROM vs 精确/需ROM）。
6. 交互：嵌 `<SwiCallDemo />`，单步看一次 Div SWI 的 HLE 处理。
7. 下集预告：还剩最后一块——这一帧画面配的声音，是怎么和画面同步发出来的？引出第 9 集《声音：4+2 个声道如何合成一帧音频》。

## 5. 站点改动

- 新增 `website/guide/ep08-bios.md`。
- 新增 `website/components/SwiCallDemo.vue` + theme/index.ts 注册（保留前七个）。
- config.ts sidebar 加第 8 集。
- index.md 首页地图第 8 集卡片转「已上线」+ link。

## 6. 不做什么（YAGNI）

- 不逐个讲所有 SWI 调用（_unLz77/_ArcTan/各 IntrWait 等），用 Div 做主例，其余点到为止。
- 不讲 hleBios 字节数组的逐字节含义、中断向量细节。
- 不深入 biosStall（HLE 的周期补偿）的精确计算。
- SwiCallDemo 用预设输入，不做任意除法。
- 不预设句数。

## 7. 验收标准

1. `ep08-bios.md` 讲透 SWI 请求 + HLE 拦截实现 + 两条路 + HLE/LLE 权衡，含真实源码块 + GitHub 跳转（bios.c#L408）。
2. `<SwiCallDemo />` 可选预设、单步走 SWI→拦截→HLE 实现→写回结果，遵循统一规范；无文本溢出。
3. 首页第 8 集卡片可点进；sidebar 有第 8 集。
4. build 通过无死链；线上可访问 `/guide/ep08-bios`。
5. 双层结构 + 承接第 7 集 + 立住 HLE/LLE 概念 + 为第 9 集声音埋钩子。

## 8. 下一步

writing-plans → subagent 执行 → 部署 → headless 截图自查组件无溢出/规范一致 → 向 Hamber 简短汇报。
