# 第 5 集《时间的主宰：周期精确与事件调度》设计

> VitePress 文档站系列第 5 篇。系列承重墙——前四集反复出现的「周期/时钟/nextEvent」在此收口。
> 日期：2026-06-07 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 定位与承接

- 系列第 5 集，承重墙。序章的「贯穿全场的时钟」、CPU 篇 `ARMRunLoop` 的 `while (cycles < nextEvent)`、Thumb 篇的周期计费、内存篇的访存周期——这些「周期」最终都汇聚到一个事件调度器。本集讲透它，把前四集的时间线索收口。
- 调性沿用：深入浅出 · 双层结构。

## 2. 内容深度

**聚焦讲透 `src/core/timing.c` 的两个核心函数：`mTimingSchedule`（L36，登记未来事件）和 `mTimingTick`（L104，推进时间触发事件）。** 这是事件调度器的两个半边：一个负责「记下未来要做的事」，一个负责「时间到了把事做掉」。

不深入 reroot/priority 的所有边角、不讲 mTimingDeschedule 等辅助函数——用 schedule + tick 一对把调度模型讲透。

### 核心源码（已核实）

**mTimingTick（timing.c#L104）——时间推进与事件触发：**
```c
int32_t mTimingTick(struct mTiming* timing, int32_t cycles) {
	timing->masterCycles += cycles;
	uint32_t masterCycles = timing->masterCycles;
	while (timing->root) {
		struct mTimingEvent* next = timing->root;
		int32_t nextWhen = next->when - masterCycles;
		if (nextWhen > 0) {
			return nextWhen;          // 下一个事件还没到，返回还要等多久
		}
		timing->root = next->next;    // 取出已到期事件
		next->callback(timing, next->context, -nextWhen);  // 触发它
	}
	// ...
	return *timing->nextEvent;
}
```

**mTimingSchedule（timing.c#L36）——按时间排序插入事件链表（节选）：**
```c
void mTimingSchedule(struct mTiming* timing, struct mTimingEvent* event, int32_t when) {
	int32_t nextEvent = when + *timing->relativeCycles;
	event->when = nextEvent + timing->masterCycles;
	if (nextEvent < *timing->nextEvent) {
		*timing->nextEvent = nextEvent;  // 更新"最近一个事件"
	}
	// ... 沿链表找到按 when 排序的插入位置，插进去
}
```

### 要讲透的三点（文章主干）
1. **事件队列 = 按时间排序的链表**：每个部件（PPU、定时器、DMA、音频）把「我下一次要做事是在第几个周期」用 `mTimingSchedule` 登记进一条链表，链表按触发时刻 `when` 从早到晚排序。调度器不关心是谁，只关心「下一件最早要发生的事是什么、在何时」。
2. **mTimingTick：时间到了，把事做掉**：CPU 跑掉一批周期后调 `mTimingTick(cycles)`，它把主时钟 `masterCycles` 往前推，然后从链表头开始，凡是 `when` 已经到的事件，全部取出并调用其 `callback` 触发（PPU 画下一行、定时器溢出……）。遇到第一个还没到的，返回「还要等多久」。
3. **收口：这就是"贯穿全场的时钟"，也是周期精确的本质**：CPU 篇的 `while (cycles < nextEvent)` 里那个 `nextEvent`，正是 `mTimingTick` 算出的「下一个事件还有多久」。所以 CPU 不是一个周期一个周期空转着等——它**直接跑到下一个事件点**，中间一路累加周期（Thumb 篇的指令计费、内存篇的访存周期都加在这里）。没人空转，事件在精确的周期点触发，这就是「周期精确」。事件驱动 = 用「下一件事在何时」代替「逐周期轮询」。

### 锚点行号（GitHub 跳转，已核实）
- `mTimingTick`：`src/core/timing.c#L104`
- `mTimingSchedule`：`src/core/timing.c#L36`

## 3. 交互组件：`EventQueueDemo.vue`

事件队列 + 时间轴可视化。

### 形态
- **顶部**：一条横向时间轴（标周期刻度），上面摆着若干已登记的事件标记（如 PPU 画线@周期X、定时器@周期Y、DMA@周期Z），一个游标表示当前 masterCycles。
- **事件队列**：右侧/下方列出按 when 排序的事件链表（与时间轴上的标记对应）。
- **控制**：「推进到下一个事件」按钮——游标跳到下一个最近事件点（不是逐周期爬），该事件高亮触发（从队列移除、显示"触发！"），并演示它触发后又 schedule 一个新事件回队列（如 PPU 画完一行登记画下一行）。「重置」。
- **说明**：强调游标是"跳"到事件点而非逐格移动——可视化「周期精确 ≠ 逐周期模拟」。
- 预设事件序列写死。纯 Vue3+CSS，青绿暗色，标注教学示意。
- **遵循统一规范**：active/触发高亮背景 0.14；按钮 hover 0.12；选中按钮加 `.on:hover {#4af0d2}`；hex/数字 nowrap 防溢出。

### 与已有组件关系
第五个交互组件。前四个都是「单步执行/查表/路由」，这个是「时间轴 + 事件队列跳转」，可视化事件驱动调度——是本集最适合动起来的概念。

## 4. 文章结构（自然铺）

1. 开篇：收束前四集——CPU 跑、指令计费、访存计费，周期一路在加；可 PPU 何时画线、定时器何时响？谁在指挥？打开 timing.c。
2. 事件队列：mTimingSchedule，部件登记"我下次几点做事"，链表按时间排序。
3. 时间到了把事做掉：mTimingTick 推进 masterCycles、触发到期事件、返回下一个事件还有多久。
4. 收口：CPU 篇的 nextEvent 就是这里来的；CPU 直接跳到下一事件点，不空转；这就是周期精确 + 贯穿全场的时钟。
5. 交互：嵌 `<EventQueueDemo />`，推进时间看事件触发、游标跳转。
6. 下集预告：这些事件里有一个特别的——「不打扰 CPU 的搬运工」DMA，它能在 CPU 不参与的情况下高速搬数据。引出第 6 集《DMA》。

## 5. 站点改动

- 新增 `website/guide/ep05-timing.md`。
- 新增 `website/components/EventQueueDemo.vue` + theme/index.ts 注册（保留前四个）。
- config.ts sidebar 加第 5 集。
- index.md 首页地图第 5 集卡片转「已上线」+ link。

## 6. 不做什么（YAGNI）

- 不深入 reroot 机制、priority 排序细节、mTimingDeschedule 等辅助函数。
- 不讲各部件具体 callback 的内部实现（PPU/定时器留各自集）。
- EventQueueDemo 用写死事件序列，不做任意事件编辑。
- 不预设句数。

## 7. 验收标准

1. `ep05-timing.md` 讲透事件队列 + tick 触发 + 收口（nextEvent/周期精确），含真实源码块 + 2 个 GitHub 跳转（timing.c#L104、timing.c#L36）。
2. `<EventQueueDemo />` 可推进时间、游标跳到下一事件点、事件触发高亮、演示重新入队；遵循统一高亮/hover 规范；无文本溢出。
3. 首页第 5 集卡片可点进；sidebar 有第 5 集。
4. build 通过无死链；线上可访问 `/guide/ep05-timing`。
5. 双层结构 + 收口前四集周期线索 + 为第 6 集 DMA 埋钩子。

## 8. 下一步

writing-plans → subagent 执行 → 部署 → headless 截图自查组件无溢出/高亮规范一致 → 向 Hamber 简短汇报。
