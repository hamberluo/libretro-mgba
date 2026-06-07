# 时间的主宰 · 周期精确与事件调度

前四集，我们一路在累加周期：CPU 取指执行计费、一条 ADD 花基础周期、一次访存按地址区域花周期。可是——CPU 一路跑下去，PPU 什么时候该画下一行？定时器什么时候该响？是谁在协调这些部件，让它们在**正确的周期点**上各司其职？

序章说有「一根贯穿全场的时钟」。这一集，我们打开它的真身：`src/core/timing.c` 的事件调度器。

## 一、事件队列：登记「我下次几点要做事」

调度器的核心是一条**按时间排序的链表**。每个部件把「我下一次要做事，是在第几个周期」用 `mTimingSchedule` 登记进来：

```c
void mTimingSchedule(struct mTiming* timing, struct mTimingEvent* event, int32_t when) {
	int32_t nextEvent = when + *timing->relativeCycles;
	event->when = nextEvent + timing->masterCycles;
	if (nextEvent < *timing->nextEvent) {
		*timing->nextEvent = nextEvent;   // 更新"最近的下一个事件"
	}
	// ... 沿链表找到按 when 排序的位置，把事件插进去
}
```

> ↗ 源码：[`src/core/timing.c#L36`](https://github.com/hamberluo/libretro-mgba/blob/master/src/core/timing.c#L36)

链表按触发时刻 `when` 从早到晚排序。调度器不关心事件是谁登记的，只关心一件事：**下一件最早要发生的事，是什么、在何时。** PPU 画完一行，会登记「画下一行」；定时器溢出，会登记「下次溢出」——部件自己负责把未来的自己排进队列。

## 二、时间到了，把事做掉

CPU 跑掉一批周期后，调 `mTimingTick`：

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
		timing->root = next->next;    // 取出已到期的事件
		next->callback(timing, next->context, -nextWhen);  // 触发它
	}
	return *timing->nextEvent;
}
```

> ↗ 源码：[`src/core/timing.c#L104`](https://github.com/hamberluo/libretro-mgba/blob/master/src/core/timing.c#L104)

它做两件事：先把主时钟 `masterCycles` 往前推 `cycles`；然后从链表头开始，凡是 `when` 已经到的事件，**全部取出、调用 callback 触发**（PPU 画下一行、定时器溢出处理……）。一遇到还没到的事件，就返回「还要等多久」（`nextWhen`）。

## 三、收口：这就是「贯穿全场的时钟」

还记得 CPU 篇那个循环吗？

```c
while (cpu->cycles < cpu->nextEvent) {
	ARMStep(cpu);
}
```

那个 `nextEvent`，正是 `mTimingTick` 返回的「下一个事件还有多久」。所以 CPU **不是一个周期一个周期空转着等**——它一口气跑到下一个事件点，中间一路累加周期（Thumb 篇的指令计费、内存篇的访存周期，全加在这里）。跑到了，停下来，`mTimingTick` 把到期的事件触发掉，再算出下一个事件点，CPU 接着冲。

这就是**事件驱动**：用「下一件事在何时」代替「逐周期轮询」。也是**周期精确**的本质——没有谁空转浪费，每个事件都在它该触发的那个精确周期点上被触发。序章那根「让所有部件对上拍子」的时钟，就是这条排好序的事件队列加上 tick。

## 四、动手试试：让 CPU 跳到下一个事件点

下面这个组件，时间轴上摆着几个已登记的事件（PPU 画线、定时器、DMA）。点「推进」，看游标如何**跳**到下一个最近的事件点（而不是一格格爬），事件触发后又把自己的下一次登记回队列：

<EventQueueDemo />

## 下一集

队列里有一类特别的事件——**DMA 传输**。它是个「不打扰 CPU 的搬运工」：CPU 发起后，它能在 CPU 几乎不参与的情况下，把大块数据高速从一处搬到另一处（比如把图像数据搬进显存）。它凭什么能「抢」过 CPU 占用总线？下一集，《DMA：不打扰 CPU 的搬运工》。
