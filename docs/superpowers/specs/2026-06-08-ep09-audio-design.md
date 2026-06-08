# 第 9 集《声音：4+2 个声道如何合成一帧音频》设计

> VitePress 文档站系列第 9 篇。承接第 8 集结尾的钩子：声音怎么生成、又怎么和画面同步。
> 日期：2026-06-08 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 定位与承接

- 系列第 9 集。第 8 集解开了 BIOS 之谜，留最后一块拼图：这一帧画面配的声音，怎么生成、怎么和画面同步？本集打开 `audio.c` 的 `GBAAudioSample` 回答。
- 收束：第 5 集事件调度（采样是事件）、第 6 集 DMA（FIFO 声道靠 DMA 喂数据）、GB 血统（4 个 PSG 声道）。
- 调性沿用：深入浅出 · 双层结构。

## 2. 内容深度

**聚焦讲透 `src/gba/audio.c` 的 `GBAAudioSample`（L353）混音循环 + `_sample`（L401）采样事件。** 不深入 PSG 各声道（方波/波形/噪声）的波形生成细节、FIFO 的 DMA 喂数机制内部——讲「6 声道如何混成一个立体声采样 + 怎么和画面同步」这一层。

### 核心源码（已核实，节选自 GBAAudioSample）

```c
void GBAAudioSample(struct GBAAudio* audio, int32_t timestamp) {
	// ... 对每个待生成的采样：
	int16_t sampleLeft = 0;
	int16_t sampleRight = 0;
	int psgShift = 4 - audio->volume;

	// 1) 先混 4 个 PSG 声道（GB 时代继承的方波/波形/噪声）
	GBAudioSamplePSG(&audio->psg, &sampleLeft, &sampleRight);
	sampleLeft >>= psgShift;
	sampleRight >>= psgShift;

	// 2) 叠加 FIFO 声道 A（GBA 新增的 DMA 数字音频）
	if (audio->chALeft)  { sampleLeft  += (audio->chA.samples[sample] << 2) >> !audio->volumeChA; }
	if (audio->chARight) { sampleRight += (audio->chA.samples[sample] << 2) >> !audio->volumeChA; }

	// 3) 叠加 FIFO 声道 B
	if (audio->chBLeft)  { sampleLeft  += (audio->chB.samples[sample] << 2) >> !audio->volumeChB; }
	if (audio->chBRight) { sampleRight += (audio->chB.samples[sample] << 2) >> !audio->volumeChB; }

	sampleLeft  = _applyBias(audio, sampleLeft);
	sampleRight = _applyBias(audio, sampleRight);
	audio->currentSamples[sample].left  = sampleLeft;
	audio->currentSamples[sample].right = sampleRight;
}
```

**_sample（L401）——采样事件回调：**
```c
static void _sample(struct mTiming* timing, void* user, uint32_t cyclesLate) {
	struct GBAAudio* audio = user;
	GBAAudioSample(audio, mTimingCurrentTime(&audio->p->timing) - cyclesLate);
	// ... 把这批采样写进音频缓冲
}
```

### 要讲透的三点（文章主干）
1. **4 + 2 声道：两代血统**。GBA 的声音有 6 个声道，分两类：
   - **4 个 PSG 声道**：从 Game Boy 继承来的——两个方波、一个可编程波形、一个噪声。靠寄存器描述音色，是「合成」出来的音。
   - **2 个 FIFO 声道（chA/chB）**：GBA 新增的「直接声音」——播放的是 8 位 PCM 数字采样，靠 **DMA**（第 6 集）从内存源源不断喂进 FIFO 队列。游戏音乐和语音多走这两路。
2. **混音 = 各声道波形相加**。看 `GBAAudioSample`：先取 4 个 PSG 声道的混合值，再把 chA、chB 的采样**按音量移位后加上去**，左右声道各自累加。声音合成的本质，朴素到不可思议——**就是把各路声音的波形数值加在一起**（再做个 bias 偏置）。一个立体声采样 = (sampleLeft, sampleRight)。
3. **音视频同步的真相：同一根时钟**（承接第 8 集钩子、收束第 5 集）。`_sample` 是一个**事件回调**——和 PPU 画扫描线（第 7 集）、DMA 传输（第 6 集）一样，它按固定的 `sampleInterval` 被 `mTimingSchedule` 登记进**第 5 集那条同一条事件队列**。所以音频和视频不是被「特意对齐」的，而是**两者都由同一根主时钟驱动**：CPU 跑够周期，该出采样就出采样，该画一行就画一行。同步是「同源」的自然结果。

### 锚点行号（GitHub 跳转，已核实）
- `GBAAudioSample`：`src/gba/audio.c#L353`
- `_sample`：`src/gba/audio.c#L401`

## 3. 交互组件：`MixerDemo.vue`

混音可视化——调声道看合成输出。

### 形态
- **6 个声道行**：4 个 PSG（方波1/方波2/波形/噪声）+ 2 个 FIFO（chA/chB）。每行一个开关（开/关）+ 一个示意波形值（如 +30 / -20）。
- **输出**：底部显示 `sampleLeft / sampleRight` = 所有开启声道的值之和（实时随开关变化）。用一个简单的合成波形条或数值表示。
- **控制**：点各声道开关切换；显示「= 各声道相加」的求和过程。「重置」（全开）。
- 说明：强调「混音就是相加」，关掉某声道输出立刻变化。
- 纯 Vue3+CSS，青绿暗色，标注教学示意。
- **遵循统一规范**：开启声道 active 高亮 0.14、按钮 hover 0.12、选中按钮 `.on:hover {#4af0d2}`、数字 nowrap。PSG 与 FIFO 两组可用不同强调色（PSG 青绿、FIFO 蓝）区分两代血统。

### 与已有组件关系
第九个交互组件。表现「多路相加」的混音概念，与前面的单步/路由/队列类不同，是「多开关→看合成」的实时聚合。

## 4. 文章结构（自然铺）

1. 开篇：第 8 集解开 BIOS，最后一块拼图——这帧画面的声音怎么来、怎么同步？打开 audio.c。
2. 4+2 声道两代血统：4 PSG（GB 继承，合成音）+ 2 FIFO（GBA 新增，DMA 喂的数字采样）。
3. 混音就是相加：GBAAudioSample 把各声道波形按音量加成 left/right。
4. 音视频同步的真相：_sample 是事件回调，和 PPU/DMA 同挂第 5 集那根时钟，同源即同步。
5. 交互：嵌 `<MixerDemo />`，开关声道看输出实时变化。
6. 下集预告（系列收尾铺垫）：一台机器跑起来了——画面、声音、时序都有了。最后一个问题：怎么把这台正在运行的机器「冻」在某一瞬间，之后还能精确还原？引出第 10 集《随时存档读档：把整台机器冻在一瞬间》（系列收官）。

## 5. 站点改动

- 新增 `website/guide/ep09-audio.md`。
- 新增 `website/components/MixerDemo.vue` + theme/index.ts 注册（保留前八个）。
- config.ts sidebar 加第 9 集。
- index.md 首页地图第 9 集卡片转「已上线」+ link。

## 6. 不做什么（YAGNI）

- 不深入 PSG 各声道波形生成（方波占空比、噪声 LFSR、波形 RAM）的细节。
- 不讲 FIFO 的 DMA 喂数时序、SOUNDBIAS/分辨率寄存器细节。
- 不讲 _applyBias 的精确偏置算法。
- MixerDemo 用示意波形值（写死几个数），不做真实音频合成/播放。
- 不预设句数。

## 7. 验收标准

1. `ep09-audio.md` 讲透 4+2 声道两代血统 + 混音相加 + 音视频同步（同一时钟），含真实源码块 + 2 个 GitHub 跳转（audio.c#L353、audio.c#L401）。
2. `<MixerDemo />` 6 声道可开关、输出 left/right 实时随开关求和变化，遵循统一规范；无文本溢出。
3. 首页第 9 集卡片可点进；sidebar 有第 9 集。
4. build 通过无死链；线上可访问 `/guide/ep09-audio`。
5. 双层结构 + 承接第 8 集 + 收束第 5/6 集 + 为第 10 集（收官）埋钩子。

## 8. 下一步

writing-plans → subagent 执行 → 部署 → headless 截图自查组件无溢出/规范一致 → 向 Hamber 简短汇报。
