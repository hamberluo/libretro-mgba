# PPU · 扫描线是怎么画出来的

上一集，DMA 把图像数据高速搬进了 VRAM（显存）。可数据进了显存，它还只是一堆字节——谁把它变成屏幕上你看到的、一行行的像素？这一集，我们打开 `video.c`，看 PPU 怎么一行行把画面画出来。

## 一、扫描线，是两个交替的事件

PPU 不是「啪」一下把整屏画出来的。它一行一行地画，而「画一行」和「这行画完的间隙」，是两个互相接力的事件：

```c
void _startHblank(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	struct GBAVideo* video = context;
	video->event.callback = _startHdraw;
	mTimingSchedule(timing, &video->event, VIDEO_HBLANK_LENGTH - cyclesLate);  // 登记下一次
	// ...
}
```

> ↗ 源码：[`src/gba/video.c#L205`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/video.c#L205)

`_startHdraw`（开始画一行）和 `_startHblank`（这行画完、进入水平消隐 HBlank）互相把对方 `mTimingSchedule` 进事件队列——**这正是第 5 集那条时间线的活实例**。PPU 是事件队列最勤快的常客：画一行、消隐、再排下一行，周而复始。CPU 一路跑指令，跑到 PPU 这个事件的周期点，就轮到 PPU 画一行。

## 二、drawScanline：在消隐期把这一行画出来

`_startHblank` 真正画像素的就一句：

```c
// 在水平消隐期，把当前这一行画出来
if (video->vcount < GBA_VIDEO_VERTICAL_PIXELS && video->frameskipCounter <= 0) {
	video->renderer->drawScanline(video->renderer, video->vcount);
}
if (video->vcount < GBA_VIDEO_VERTICAL_PIXELS) {
	GBADMARunHblank(video->p, -cyclesLate);   // HBlank 也会触发 DMA
}
```

`drawScanline(renderer, video->vcount)`——把第 `vcount` 行的像素，从 VRAM 里的背景层、精灵层数据合成出来。一帧画面有 160 行（`GBA_VIDEO_VERTICAL_PIXELS`），就是 160 次 `drawScanline`。序章里那条从屏幕顶端缓缓走到底端的扫描线，本质就是 `vcount` 从 0 数到 159。

（注意 `GBADMARunHblank`——第 6 集说 DMA 爱挑 HBlank 时机，这里就是它被调用的地方。）

## 三、vcount 到 160：一帧诞生，VBlank 登场

谁来推进行号、谁来宣布「一帧画完了」？是 `_startHdraw`：

```c
void _startHdraw(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	struct GBAVideo* video = context;
	video->event.callback = _startHblank;
	mTimingSchedule(timing, &video->event, VIDEO_HDRAW_LENGTH - cyclesLate);

	++video->vcount;                              // 行号 +1
	switch (video->vcount) {
	case GBA_VIDEO_VERTICAL_PIXELS:               // 画到第 160 行 = 一帧画完
		video->renderer->finishFrame(video->renderer);
		GBADMARunVblank(video->p, -cyclesLate);   // VBlank 触发 DMA
		// ... 触发 VBlank 中断
		break;
	}
}
```

> ↗ 源码：[`src/gba/video.c#L148`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/video.c#L148)

每画完一行，`++vcount`。当 vcount 数到 160——可见的 160 行全画完了，`finishFrame` 把这一帧提交出去（你眼前的画面就此点亮），然后进入 **VBlank（垂直消隐）**：触发 VBlank 中断、调用 `GBADMARunVblank`。

VBlank 是个黄金窗口：屏幕这会儿不画新东西，游戏正好趁机更新下一帧的逻辑、用 DMA 批量把新图像数据搬进显存——这就是第 6 集说的「DMA 挑 VBlank 时机」的全貌。VBlank 过后 vcount 归零，新一帧从第 0 行重新开始。

## 四、动手试试：看一帧一行行画出来

下面这个组件，点「画下一行」或「自动播放」，看扫描线从上往下推进、一帧逐渐成形，画到底进入 VBlank：

<ScanlineDemo />

## 主线走完了

到这里，序章问的那个问题——「一帧画面是怎么诞生的」——我们已经完整走了一遍：你按键，CPU 取指执行（第 2、3 集），读写内存（第 4 集），一根时钟在背后调度一切（第 5 集），DMA 高速搬运数据进显存（第 6 集），PPU 一行行把它画成像素（本集）。一帧，就这么诞生了。

但还有两块拼图没拼上：游戏一开机，连 BIOS 都还没运行，它是怎么跑起来的？声音又是怎么和这一帧画面同步发出来的？下一集，《没有真 BIOS，游戏怎么还能跑？》。
