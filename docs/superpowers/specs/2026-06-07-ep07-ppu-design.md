# 第 7 集《PPU：扫描线是怎么画出来的》设计

> VitePress 文档站系列第 7 篇。承接第 6 集「数据进了显存怎么变像素」钩子，兑现序章的扫描线意象，是「一帧的诞生」主线的视觉终点。
> 日期：2026-06-07 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 定位与承接

- 系列第 7 集。第 6 集 DMA 把图像数据搬进 VRAM，留钩子：数据进了显存，怎么变成屏幕上一行行像素？本集打开 `video.c` 的扫描线驱动回答。
- 这集是主线视觉终点，收束三处：序章「一行一行画扫描线」意象、第 5 集事件调度（扫描线就是事件）、第 6 集 DMA（在 HBlank/VBlank 触发，本集看到它被调用）。
- 调性沿用：深入浅出 · 双层结构。

## 2. 内容深度

**聚焦讲透 `src/gba/video.c` 的一对扫描线事件回调：`_startHdraw`（L148，开始画一行）和 `_startHblank`（L205，水平消隐 + 画出该行）。** 不深入软件渲染器内部（renderers/ 各 mode 的像素合成、背景层/精灵层混合）——那是更细的实现，本集讲「扫描线节奏」这一层。

### 核心源码（已核实，节选）

**_startHblank（L205）——画出当前行：**
```c
void _startHblank(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	struct GBAVideo* video = context;
	video->event.callback = _startHdraw;
	mTimingSchedule(timing, &video->event, VIDEO_HBLANK_LENGTH - cyclesLate);  // 登记下一次

	// 在水平消隐期，把当前这一行画出来
	if (video->vcount < GBA_VIDEO_VERTICAL_PIXELS && video->frameskipCounter <= 0) {
		video->renderer->drawScanline(video->renderer, video->vcount);
	}
	if (video->vcount < GBA_VIDEO_VERTICAL_PIXELS) {
		GBADMARunHblank(video->p, -cyclesLate);   // HBlank 触发 DMA（呼应第 6 集）
	}
}
```

**_startHdraw（L148）——推进到下一行、管帧边界：**
```c
void _startHdraw(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	struct GBAVideo* video = context;
	video->event.callback = _startHblank;
	mTimingSchedule(timing, &video->event, VIDEO_HDRAW_LENGTH - cyclesLate);  // 登记下一次

	++video->vcount;                              // 行号 +1
	// ...
	switch (video->vcount) {
	case GBA_VIDEO_VERTICAL_PIXELS:               // 画到第 160 行 = 一帧画完
		video->renderer->finishFrame(video->renderer);
		GBADMARunVblank(video->p, -cyclesLate);   // VBlank 触发 DMA（呼应第 6 集）
		// ... 触发 VBlank 中断
		break;
	}
}
```

### 要讲透的三点（文章主干）
1. **扫描线 = 两个交替的事件**：`_startHdraw`（开始画一行）和 `_startHblank`（这行画完、进入水平消隐）互相把对方 `mTimingSchedule` 进事件队列。PPU 不是一口气画完整屏——它**一行一行地排进第 5 集那条时间线**，画一行、消隐、再画下一行。这就是第 5 集事件调度的活生生实例：PPU 是事件队列最勤快的常客。
2. **drawScanline：在消隐期画出这一行**：`_startHblank` 里 `video->renderer->drawScanline(renderer, video->vcount)`——把第 `vcount` 行的像素，从 VRAM 的数据合成出来。一帧 160 行，就是 160 次 drawScanline。序章那条从上到下的扫描线，就是 vcount 从 0 数到 159。
3. **vcount 到 160：一帧诞生，VBlank 登场**：`_startHdraw` 里 `++vcount`，当 vcount 到 `GBA_VIDEO_VERTICAL_PIXELS`（160）——可见行画完了，`finishFrame` 提交这一帧，进入 **VBlank**（垂直消隐），触发 VBlank 中断、`GBADMARunVblank`（第 6 集说的「DMA 挑 VBlank 时机」就在这里被调用！）。VBlank 是游戏更新下一帧、批量 DMA 搬数据的黄金窗口。然后 vcount 继续走完总行数、归零，新一帧开始。

### 锚点行号（GitHub 跳转，已核实）
- `_startHblank`：`src/gba/video.c#L205`
- `_startHdraw`：`src/gba/video.c#L148`

## 3. 交互组件：`ScanlineDemo.vue`

扫描线绘制可视化——兑现序章扫描线意象，可交互版。

### 形态
- **一块 GBA 屏幕**（用 CSS grid 画一个简化的行格，如 16 行代表 160 行的缩影，或直接 160 细行）：已画的行点亮（渐变色示意画面内容），当前行高亮（扫描线），未画的行暗。
- **状态**：显示 `vcount`（当前行号 0-159）、当前阶段（HDraw 画线 / HBlank 消隐 / VBlank）。
- **控制**：「画下一行」单步推进 vcount + 点亮该行；「自动播放」连续画完一帧；到 160 行进入 VBlank（整屏点亮 + 提示「一帧完成！进入 VBlank，DMA 可以搬下一帧数据了」）。「重置」。
- 纯 Vue3+CSS，青绿暗色，标注教学示意。
- **遵循统一规范**：当前行/active 高亮 0.14 或扫描线用 ACCENT；按钮 hover 0.12；选中按钮 `.on:hover {#4af0d2}`；数字 nowrap。VBlank 提示可用 GOLD。

### 与已有组件关系
第七个交互组件。最贴近「画面」主题，是六个组件里视觉上最像「成果」的一个——它画出的就是观众一开始想看到的那帧画面的诞生过程。

## 4. 文章结构（自然铺）

1. 开篇：第 6 集 DMA 把数据搬进了显存，可它还是一堆字节——谁把它变成屏幕上的像素？打开 video.c。
2. 扫描线是两个交替的事件：_startHdraw ↔ _startHblank 互相 schedule，PPU 是第 5 集事件队列的常客。
3. drawScanline：在 HBlank 把第 vcount 行画出来，一帧 160 次。
4. vcount 到 160：finishFrame 一帧诞生，VBlank 登场，GBADMARunVblank（收束第 6 集）。
5. 交互：嵌 `<ScanlineDemo />`，画下一行/自动播放看扫描线推进、一帧诞生。
6. 收束 + 下集预告：到这里，「一帧画面是怎么诞生的」这条主线走完了——从按键、CPU、内存、时钟、DMA 到 PPU 画线。但还有两块拼图：游戏一开机，连 BIOS 都没有它怎么跑起来的？引出第 8 集《没有真 BIOS，游戏怎么还能跑？》。

## 5. 站点改动

- 新增 `website/guide/ep07-ppu.md`。
- 新增 `website/components/ScanlineDemo.vue` + theme/index.ts 注册（保留前六个）。
- config.ts sidebar 加第 7 集。
- index.md 首页地图第 7 集卡片转「已上线」+ link。

## 6. 不做什么（YAGNI）

- 不深入软件渲染器（renderers/software-*.c）的像素合成、背景模式（mode 0-5）、精灵/窗口/混合等细节。
- 不讲 DISPSTAT/DISPCNT 各寄存器位的全部含义。
- ScanlineDemo 用简化行数表现，不做真实 240×160 逐像素。
- 不预设句数。

## 7. 验收标准

1. `ep07-ppu.md` 讲透扫描线双事件 + drawScanline + vcount到160/VBlank，含真实源码块 + 2 个 GitHub 跳转（video.c#L205、video.c#L148）。
2. `<ScanlineDemo />` 可单步画行/自动播放、vcount 推进、当前行高亮、到 160 进 VBlank 整屏点亮，遵循统一规范；无文本溢出。
3. 首页第 7 集卡片可点进；sidebar 有第 7 集。
4. build 通过无死链；线上可访问 `/guide/ep07-ppu`。
5. 双层结构 + 承接第 6 集 + 收束序章/第5/6集 + 主线视觉收尾 + 为第 8 集 BIOS 埋钩子。

## 8. 下一步

writing-plans → subagent 执行 → 部署 → headless 截图自查组件无溢出/规范一致 → 向 Hamber 简短汇报。
