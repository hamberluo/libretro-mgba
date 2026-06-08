# 声音 · 4+2 个声道如何合成一帧音频

<AudioPlayer src="audio/ep09-audio.mp3" />

第 8 集解开了 BIOS 之谜。现在整台机器几乎都拼齐了——画面、时序、搬运、启动。还剩最后一块：你看到这一帧画面时，配的那段声音是怎么生成的？又是怎么和画面**同步**发出来的？打开 `audio.c`。

## 一、4 + 2 个声道：两代血统

GBA 的声音有 6 个声道，分成两类，藏着它的身世：

- **4 个 PSG 声道**——直接从 **Game Boy** 继承来的：两个方波、一个可编程波形、一个噪声。它们靠寄存器描述音色，声音是「合成」出来的（电子味的滴滴声就来自这里）。
- **2 个 FIFO 声道（chA / chB）**——GBA 新增的「直接声音」：播放的是 8 位 PCM 数字采样，靠 **DMA**（第 6 集那个搬运工）从内存源源不断喂进一个 FIFO 队列。游戏的背景音乐和语音，大多走这两路。

老 4 路负责音效，新 2 路负责音乐——这就是「4+2」。

## 二、混音，就是把波形加起来

这 6 路声音怎么变成一路输出？看 `GBAAudioSample`：

```c
int16_t sampleLeft = 0;
int16_t sampleRight = 0;

// 1) 先混 4 个 PSG 声道
GBAudioSamplePSG(&audio->psg, &sampleLeft, &sampleRight);

// 2) 叠加 FIFO 声道 A
if (audio->chALeft)  { sampleLeft  += (audio->chA.samples[sample] << 2) >> !audio->volumeChA; }
if (audio->chARight) { sampleRight += (audio->chA.samples[sample] << 2) >> !audio->volumeChA; }

// 3) 叠加 FIFO 声道 B
if (audio->chBLeft)  { sampleLeft  += (audio->chB.samples[sample] << 2) >> !audio->volumeChB; }
if (audio->chBRight) { sampleRight += (audio->chB.samples[sample] << 2) >> !audio->volumeChB; }

audio->currentSamples[sample].left  = sampleLeft;
audio->currentSamples[sample].right = sampleRight;
```

> ↗ 源码：[`src/gba/audio.c#L353`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/audio.c#L353)

朴素到不可思议：先取 4 个 PSG 声道的混合值，再把 chA、chB 按音量**加上去**，左右声道各自累加。**混音的本质，就是把各路声音的波形数值相加。** 一个立体声采样，就是一对 `(sampleLeft, sampleRight)`。

## 三、音视频同步的真相：同一根时钟

声音怎么和画面对得上？答案出人意料地简单——看采样是怎么被触发的：

```c
static void _sample(struct mTiming* timing, void* user, uint32_t cyclesLate) {
	struct GBAAudio* audio = user;
	GBAAudioSample(audio, mTimingCurrentTime(&audio->p->timing) - cyclesLate);
	// ... 把这批采样写进音频缓冲
}
```

> ↗ 源码：[`src/gba/audio.c#L401`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/audio.c#L401)

`_sample` 是一个**事件回调**——和 PPU 画扫描线（第 7 集）、DMA 传输（第 6 集）一模一样，它按固定的采样间隔被 `mTimingSchedule` 登记进**第 5 集那条同一条事件队列**。

所以音频和视频根本不需要被「特意对齐」：**它们都由同一根主时钟驱动**。CPU 跑够周期，该出一个采样就出采样，该画一行就画一行，该 DMA 就 DMA。同步，是「同源」的自然结果——这正是第 5 集那个事件调度器的威力：它让整台机器的所有部件，踩着同一个节拍走。

## 四、动手试试：混音就是相加

下面这个混音台，6 个声道（青绿是 4 个 PSG，蓝色是 2 个 FIFO）。点任意声道静音/开启，看底部的输出采样值随之变化——它永远等于所有开启声道的数值之和：

<MixerDemo />

## 下一集（系列收官）

到这里，一台 GBA 在 mGBA 里是怎么活过来的，我们基本看全了：CPU 执行指令、内存路由、时钟调度、DMA 搬运、PPU 画面、BIOS 兜底、声音合成。最后一个问题，也是模拟器独有的超能力：怎么把这台正在全速运行的机器，**精确地「冻」在某一瞬间**，关掉再打开还能分毫不差地接着跑？下一集，也是本系列最后一集，《随时存档读档：把整台机器冻在一瞬间》。
