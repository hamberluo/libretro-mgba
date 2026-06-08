# 第 10 集《随时存档读档：把整台机器冻在一瞬间》实现计划（系列收官）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 文档站新增第 10 集（收官）存档篇：讲透 `GBASerialize` 快照、状态即变量（回顾全系列）、versionMagic 防坑，配 `SaveStateDemo` 存读档可视化组件，接线、确保首页 10 集全部上线，部署。

**Architecture:** 沿用已上线 VitePress 站点（`website/`）。组件+正文+接线合并由一个 implementer 完成 → vitepress build（python 子串校验）→ headless 截图查溢出/规范 + 检查首页无残留【敬请期待】 → 合并 master → push 部署。

**Tech Stack:** VitePress 1.6.4、Vue 3、已有 GitHub Actions 部署。

**关键事实（已核实）：**
- GitHub blob 前缀：`https://github.com/hamberluo/libretro-mgba/blob/master/`
- `GBASerialize`：`src/gba/serialize.c#L27`；`GBADeserialize`：`src/gba/serialize.c#L96`。
- 组件全局注册在 `website/.vitepress/theme/index.ts`（已注册 9 个）。
- 部署触发：push master 改 `website/**`。
- **统一组件规范**：active/变化高亮 `rgba(0,212,170,0.14)`+`#00d4aa`；按钮 hover `rgba(0,212,170,0.12)`；选中按钮 `.on:hover { background:#4af0d2; color:#0d1b2a }`；数字 nowrap；GOLD `#ffd166` 强调快照。验证用 python 子串；上线后 headless 截图自查。
- **收官特别项**：这是第 10 集，上线后首页 10 集地图应全部可点、无【敬请期待】残留。

**「测试」语义：** 前端站点。判据：vitepress build 无死链 + python 子串校验命中 + headless 截图确认无溢出、规范一致、首页无残留占位。

---

## File Structure

| 文件 | 责任 |
|------|------|
| `website/components/SaveStateDemo.vue` | 存读档可视化：运行→存档(快照)→再运行→读档(精确还原) |
| `website/.vitepress/theme/index.ts` | 增注册 SaveStateDemo |
| `website/guide/ep10-savestate.md` | 存档篇正文（含系列总收束） |
| `website/.vitepress/config.ts` | sidebar 增第 10 集 |
| `website/index.md` | 首页地图第 10 集卡片转已上线 |

---

## Task 1: SaveStateDemo 组件 + 正文 + 接线（合并执行）

**Files:**
- Create: `website/components/SaveStateDemo.vue`、`website/guide/ep10-savestate.md`
- Modify: `website/.vitepress/theme/index.ts`、`website/.vitepress/config.ts`、`website/index.md`

### Step 1: 创建 website/components/SaveStateDemo.vue

```vue
<script setup>
import { ref, reactive, computed } from 'vue'

// 运行中机器的几个关键状态（示意）
const live = reactive({ pc: 0x8000000, r0: 0, cycles: 0, vcount: 0, samples: 0 })
const snapshot = ref(null)   // 快照（null = 尚无存档）
const restoredFlash = ref(false)

function run() {
  // 让状态往前跑一点（示意：各自变化）
  live.pc += 4
  live.r0 = (live.r0 + 7) & 0xFF
  live.cycles += 280
  live.vcount = (live.vcount + 1) % 160
  live.samples += 32
}
function save() {
  snapshot.value = { ...live, magic: '0x01000004' }   // 拍快照 + 版本魔数
}
function load() {
  if (!snapshot.value) return
  const s = snapshot.value
  live.pc = s.pc; live.r0 = s.r0; live.cycles = s.cycles
  live.vcount = s.vcount; live.samples = s.samples
  restoredFlash.value = true
  setTimeout(() => { restoredFlash.value = false }, 600)
}
function reset() {
  live.pc = 0x8000000; live.r0 = 0; live.cycles = 0; live.vcount = 0; live.samples = 0
  snapshot.value = null
}

const hx = (v) => '0x' + (v >>> 0).toString(16).toUpperCase()
const rows = computed(() => [
  { k: 'PC（程序计数器）', v: hx(live.pc), s: snapshot.value ? hx(snapshot.value.pc) : '—', ep: '第2集' },
  { k: 'r0（寄存器）', v: live.r0, s: snapshot.value ? snapshot.value.r0 : '—', ep: '第2集' },
  { k: 'masterCycles（时钟）', v: live.cycles, s: snapshot.value ? snapshot.value.cycles : '—', ep: '第5集' },
  { k: 'vcount（PPU 行）', v: live.vcount, s: snapshot.value ? snapshot.value.vcount : '—', ep: '第7集' },
  { k: 'audio samples', v: live.samples, s: snapshot.value ? snapshot.value.samples : '—', ep: '第9集' },
])
</script>

<template>
  <div class="ss">
    <div class="cols">
      <div class="machine" :class="{ flash: restoredFlash }">
        <div class="mhead">运行中的机器</div>
        <div v-for="(r, i) in rows" :key="i" class="srow">
          <span class="k">{{ r.k }} <em>{{ r.ep }}</em></span>
          <span class="v">{{ r.v }}</span>
        </div>
      </div>
      <div class="snap">
        <div class="mhead">存档快照</div>
        <template v-if="snapshot">
          <div class="magic">magic {{ snapshot.magic }}</div>
          <div v-for="(r, i) in rows" :key="i" class="srow">
            <span class="k">{{ r.k.split('（')[0] }}</span>
            <span class="v gold">{{ r.s }}</span>
          </div>
        </template>
        <div v-else class="empty">尚无存档，点「存档」拍下快照</div>
      </div>
    </div>
    <div class="controls">
      <button @click="run">运行一下</button>
      <button @click="save">存档</button>
      <button @click="load" :disabled="!snapshot">读档</button>
      <button @click="reset">重置</button>
    </div>
    <p class="hint">教学示意。存档 = 把每个部件的状态变量抄进快照；读档 = 把快照值精确抄回去。整台机器没有藏在硬件里的状态，全是变量——所以能被一次性冻结、还原。真机做不到。</p>
  </div>
</template>

<style scoped>
.ss { border: 1px solid var(--vp-c-divider); border-radius: 12px; padding: 1.2rem; margin: 1.5rem 0; background: var(--vp-c-bg-soft); }
.cols { display: flex; gap: 1rem; }
.machine, .snap { flex: 1; min-width: 0; border: 1px solid var(--vp-c-divider); border-radius: 8px; padding: 0.8rem; transition: all 0.3s ease; }
.machine.flash { border-color: #00d4aa; background: rgba(0, 212, 170, 0.14); }
.mhead { font-size: 0.82rem; color: var(--vp-c-text-3); margin-bottom: 0.6rem; }
.srow { display: flex; justify-content: space-between; align-items: baseline; gap: 0.5rem; padding: 0.3rem 0; font-size: 0.82rem; }
.srow .k { color: var(--vp-c-text-2); }
.srow .k em { color: var(--vp-c-text-3); font-style: normal; font-size: 0.7rem; }
.srow .v { color: #00d4aa; font-family: var(--vp-font-family-mono); white-space: nowrap; }
.srow .v.gold { color: #ffd166; }
.magic { font-size: 0.75rem; color: #ffd166; font-family: var(--vp-font-family-mono); margin-bottom: 0.4rem; white-space: nowrap; }
.empty { font-size: 0.82rem; color: var(--vp-c-text-3); padding: 1rem 0; text-align: center; }
.controls { display: flex; gap: 0.6rem; margin-top: 1rem; flex-wrap: wrap; }
.controls button {
  padding: 0.4rem 1rem; border-radius: 8px; border: 1px solid #00d4aa;
  background: transparent; color: #00d4aa; cursor: pointer; font-size: 0.9rem; transition: background 0.2s;
}
.controls button:hover { background: rgba(0, 212, 170, 0.12); }
.controls button:disabled { opacity: 0.4; cursor: not-allowed; }
.hint { margin-top: 0.8rem; font-size: 0.85rem; color: var(--vp-c-text-3); }
@media (max-width: 640px) { .cols { flex-direction: column; } }
</style>
```

### Step 2: 改 website/.vitepress/theme/index.ts，在已有 9 个基础上增注册 SaveStateDemo

完整内容：
```ts
import DefaultTheme from 'vitepress/theme'
import PipelineDemo from '../../components/PipelineDemo.vue'
import ArmStepDemo from '../../components/ArmStepDemo.vue'
import ThumbAddDemo from '../../components/ThumbAddDemo.vue'
import MemoryMapDemo from '../../components/MemoryMapDemo.vue'
import EventQueueDemo from '../../components/EventQueueDemo.vue'
import DmaTransferDemo from '../../components/DmaTransferDemo.vue'
import ScanlineDemo from '../../components/ScanlineDemo.vue'
import SwiCallDemo from '../../components/SwiCallDemo.vue'
import MixerDemo from '../../components/MixerDemo.vue'
import SaveStateDemo from '../../components/SaveStateDemo.vue'
import type { Theme } from 'vitepress'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('PipelineDemo', PipelineDemo)
    app.component('ArmStepDemo', ArmStepDemo)
    app.component('ThumbAddDemo', ThumbAddDemo)
    app.component('MemoryMapDemo', MemoryMapDemo)
    app.component('EventQueueDemo', EventQueueDemo)
    app.component('DmaTransferDemo', DmaTransferDemo)
    app.component('ScanlineDemo', ScanlineDemo)
    app.component('SwiCallDemo', SwiCallDemo)
    app.component('MixerDemo', MixerDemo)
    app.component('SaveStateDemo', SaveStateDemo)
  },
} satisfies Theme
```

### Step 3: 创建 website/guide/ep10-savestate.md

完整内容如下（一字不差。含一个 ```c 代码块、一个 `<SaveStateDemo />` 组件标签、两个 `> ↗ 源码` 引用块；C 代码 tab 缩进/注释原样保留）：

````markdown
# 随时存档读档 · 把整台机器冻在一瞬间

前 9 集，我们看着一台 GBA 在 mGBA 里一点点活了过来。这一集，也是最后一集，讲一个真机绝对做不到、模拟器却轻而易举的超能力：把一台**正在全速运行**的机器，精确地「冻」在某一瞬间，关掉再打开，还能分毫不差地接着跑。打开 `serialize.c`。

## 一、存档，就是把状态变量抄一份

`GBASerialize` 做的事，直白得让人意外：

```c
void GBASerialize(struct GBA* gba, struct GBASerializedState* state) {
	STORE_32(GBASavestateMagic + GBASavestateVersion, 0, &state->versionMagic);  // 魔数+版本
	STORE_32(gba->biosChecksum, 0, &state->biosChecksum);
	STORE_32(gba->timing.masterCycles, 0, &state->masterCycles);   // 第5集：主时钟

	int i;
	for (i = 0; i < 16; ++i) {
		STORE_32(gba->cpu->gprs[i], i * 4, state->cpu.gprs);       // 第2集：16 个寄存器
	}
	STORE_32(gba->cpu->cpsr.packed, 0, &state->cpu.cpsr.packed);  // 第3集：标志位
	STORE_32(gba->cpu->nextEvent, 0, &state->cpu.nextEvent);      // 第5集：下一个事件
	STORE_32(gba->cpu->prefetch[0], 0, state->cpuPrefetch);       // 第2集：流水线预取
	STORE_32(gba->cpu->prefetch[1], 4, state->cpuPrefetch);
	// ... 接着是 memory / video / audio / dma / timers 各子系统状态
}
```

> ↗ 源码：[`src/gba/serialize.c#L27`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/serialize.c#L27)

它把 CPU 的 16 个寄存器、标志位、下一个事件、流水线预取、主时钟，加上内存、视频、音频、DMA、定时器的当前值，逐个 `STORE_32` 写进一个大结构体 `GBASerializedState`。**这个结构体，就是某一瞬间整台机器的完整快照。** 读档（`GBADeserialize`）是它的镜像：把每个字段 `LOAD_32` 还原回部件。

## 二、为什么能「冻结」——因为状态就是一堆变量

留意上面那些注释。存档存的每一样东西，我们这一路都见过：

- `gprs[16]`、`prefetch` —— CPU 的寄存器和流水线（**第 2 集**）
- `cpsr` —— 运算标志位（**第 3 集**）
- 内存各区域的字节 —— （**第 4 集**）
- `masterCycles`、`nextEvent` —— 主时钟和事件队列（**第 5 集**）
- `vcount` —— PPU 画到第几行（**第 7 集**）
- 音频、DMA、定时器状态 —— （**第 6、9 集**）

关键就在这里：**整台 GBA，没有任何「藏在硬件深处、看不见摸不着」的状态——它的一切，都是 mGBA 里明明白白的 C 变量。** 所以「冻结」不需要什么黑魔法，只要把这些变量抄一份；「还原」只要把它们抄回去。

真机做不到这件事——你没法把一颗真 CPU 此刻每个晶体管的电平都记下来。但模拟器可以，因为它把硬件翻译成了变量。这是模拟器独有的超能力，也是「即时存档」「速通回放」「调试回溯」这些功能的根基。

## 三、versionMagic：序列化的经典坑

注意存档的第一个字段，是 `GBASavestateMagic + GBASavestateVersion`——一个魔数加版本号。

读档时，第一件事就是验证它：魔数不对，说明这根本不是个 mGBA 存档；版本不对，说明存档结构变了（新版本加了字段、调了布局）。这时候必须拒绝或走兼容路径——**否则拿旧存档去填新结构，字段全部错位，还原出来的是一台精神错乱的机器**：PC 指向乱码、时钟对不上、画面花屏。

这不是 GBA 特有的问题。**任何要把结构化状态存下来、以后再读回来的系统，都要给格式带上版本号。** 配置文件、存档、网络协议、数据库 schema——同一个坑，同一个解法。

## 四、动手试试：冻结再还原

下面这个组件，模拟了几个关键状态。点「运行一下」让它们变化，「存档」拍下快照，再「运行」让机器跑偏，最后「读档」——看每个状态被精确还原回快照那一刻：

<SaveStateDemo />

> ↗ 读档源码：[`src/gba/serialize.c#L96`](https://github.com/hamberluo/libretro-mgba/blob/master/src/gba/serialize.c#L96)

## 尾声：一台 GBA 是怎么活过来的

十集走到这里，我们把一台 GBA 在 mGBA 里的一生，完整看了一遍：

- **序章**：一帧画面的诞生，是整台机器协同的结果；
- **CPU（第 2 集）**：用一个函数假装成一块 ARM7，取指、解码、执行；
- **指令（第 3 集）**：一条 Thumb 加法如何改变寄存器、留下标志位；
- **内存（第 4 集）**：地址不是大数组，是一张路由表，IO 区读的是硬件；
- **时钟（第 5 集）**：一条事件队列，让所有部件踩着同一个节拍；
- **DMA（第 6 集）**：靠阻塞 CPU 独占总线，高速搬运数据；
- **PPU（第 7 集）**：一行行扫描，画出每一帧；
- **BIOS（第 8 集）**：用 HLE 拦截系统调用，不要真 BIOS 也能跑；
- **声音（第 9 集）**：6 个声道相加，和画面共享同一根时钟；
- **存档（本集）**：因为一切都是变量，整台机器能被冻结、还原。

如果说这一路有什么反复出现的「内核手法」，是这么几个：**查表代替判断**（解码、地址路由）、**事件驱动代替轮询**（时钟调度）、**状态即变量**（寄存器、内存、存档）、**HLE 模拟功能而非过程**（BIOS）。

模拟器内核没有魔法。它只是把一块块硬件的行为，诚实地、一行一行地，翻译成了你我都能读懂的代码。看懂了它，你也就看懂了：所谓「底层」，不过是另一层可以被读懂的抽象而已。

感谢你读到这里。
````

### Step 4: 改 website/.vitepress/config.ts sidebar，在声音项后加第 10 项

找到：
```
          { text: '声音 · 4+2 个声道如何合成一帧音频', link: '/guide/ep09-audio' },
        ],
```
改为：
```
          { text: '声音 · 4+2 个声道如何合成一帧音频', link: '/guide/ep09-audio' },
          { text: '随时存档读档 · 把整台机器冻在一瞬间', link: '/guide/ep10-savestate' },
        ],
```

### Step 5: 改 website/index.md 第 10 集卡片

找到：
```
  - title: 随时存档读档 · 把整台机器冻在一瞬间
    details: 状态快照。难题：状态序列化的坑。【敬请期待】
```
改为：
```
  - title: 随时存档读档 · 把整台机器冻在一瞬间
    details: 状态快照。难题：状态序列化的坑。
    link: /guide/ep10-savestate
    linkText: 开始阅读
```

### Step 6: 全量构建 + python 子串校验 + 检查首页无残留占位

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
npx vitepress build 2>&1 | tail -10
python3 -c "
h=open('.vitepress/dist/guide/ep10-savestate.html',encoding='utf-8').read()
for k in ['GBASerialize','versionMagic','serialize.c#L27','serialize.c#L96','快照','教学示意']:
    print(k, k in h)
idx=open('.vitepress/dist/index.html',encoding='utf-8').read()
print('首页残留[敬请期待]:', '敬请期待' in idx)
"
rm -rf .vitepress/dist .vitepress/cache
```
Expected: build 成功无 dead link；python 6 项全 True（两个源码链接 + GBASerialize + versionMagic + 快照 + 教学示意）；**首页残留[敬请期待]: False**（10 集全部上线）。贴输出。

### Step 7: 分三个 commit 提交

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/components/SaveStateDemo.vue website/.vitepress/theme/index.ts
git commit -m "feat(docsite): SaveStateDemo 存读档可视化组件

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git add website/guide/ep10-savestate.md
git commit -m "content(docsite): 第10集 存档篇正文（快照+状态即变量+versionMagic+系列收束）

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git add website/.vitepress/config.ts website/index.md
git commit -m "feat(docsite): 第10集接线 sidebar 与首页地图（10集全部上线）

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git status
```

---

## Task 2: 部署上线与验证（系列收官）

**Files:** 无（合并 + push）

- [ ] **Step 1: 合并 feature 分支回 master**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git checkout master
git merge --no-ff feat/ep10-savestate -m "Merge: 第10集 存档篇上线（系列收官，10集全部完成）

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git branch -d feat/ep10-savestate
```

- [ ] **Step 2: push 触发部署**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git push origin master
```

- [ ] **Step 3: 跟踪 Actions 到成功**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
gh run list --workflow=docs.yml --limit 2
```
Expected: 最新 docs workflow completed/success。

- [ ] **Step 4: 线上验证**

```bash
curl -s -o /dev/null -w "%{http_code}\n" https://hamberluo.github.io/libretro-mgba/guide/ep10-savestate.html
curl -s https://hamberluo.github.io/libretro-mgba/guide/ep10-savestate.html | python3 -c "import sys;h=sys.stdin.read();[print(k,k in h) for k in ['GBASerialize','versionMagic','serialize.c#L27']]"
curl -s https://hamberluo.github.io/libretro-mgba/ | python3 -c "import sys;print('首页残留敬请期待:', '敬请期待' in sys.stdin.read())"
```
Expected: 200；python 校验命中；首页残留敬请期待: False。（控制器随后 headless 截图自查 SaveStateDemo 无溢出 + 首页 10 集全亮。）
