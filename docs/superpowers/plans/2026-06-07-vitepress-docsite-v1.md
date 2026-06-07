# VitePress 文档站 v1 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 搭建 GBA 内核精讲 VitePress 文档站 v1：首页（10 集地图）+ 序章一篇（图文 + 真实源码块 + GitHub 跳转）+ CPU 流水线交互组件，并通过 GitHub Actions 部署到 `https://hamberluo.github.io/libretro-mgba/`。

**Architecture:** 站点放仓库根 `website/`，与 `docs/superpowers/` 规划产物分离。VitePress 5（npm 包 `vitepress`），base 配 `/libretro-mgba/`。交互用原生 Vue 3 SFC + CSS transition，不引第三方动画库。GitHub Actions 在 push master 时构建并发布 Pages。

**Tech Stack:** VitePress（latest）、Vue 3、Node v25/npm 11、GitHub Actions、GitHub Pages。

**关键事实（已核实）：**
- remote: `git@github.com:hamberluo/libretro-mgba.git`，默认分支 master。
- GitHub blob 链接前缀：`https://github.com/hamberluo/libretro-mgba/blob/master/`
- `struct GBA` → `include/mgba/internal/gba/gba.h#L65`（开头成员 cpu/memory/video/audio/sio/timing）
- `mTimingSchedule` → `src/core/timing.c#L36`
- CPU 核源码：`src/arm/arm.c`、`src/arm/decoder-arm.c`、`src/arm/decoder-thumb.c`

**「测试」语义：** 前端站点。每个验证步骤判据为：① 命令成功退出；② `npm run docs:build` 构建无报错；③ dev server 起得来、目标页面/组件肉眼检查通过。

---

## File Structure

| 文件 | 责任 |
|------|------|
| `website/package.json` | vitepress 依赖 + dev/build/preview 脚本 |
| `website/.gitignore` | 忽略 node_modules、dist、cache |
| `website/.vitepress/config.ts` | 站点配置：title/base/nav/sidebar/搜索/主题色 |
| `website/.vitepress/theme/index.ts` | 继承默认主题 + 全局注册 PipelineDemo |
| `website/index.md` | 首页：hero + 10 集地图 features |
| `website/guide/intro.md` | 序章正文（图文 + 源码块 + 跳转 + 交互组件） |
| `website/components/PipelineDemo.vue` | CPU 取指-解码-执行 交互动画 |
| `.github/workflows/docs.yml` | 构建 website/ 并部署到 GitHub Pages |

---

## Task 1: VitePress 骨架与依赖

**Files:**
- Create: `website/package.json`、`website/.gitignore`

- [ ] **Step 1: 初始化 package.json**

创建 `website/package.json`：

```json
{
  "name": "gba-kernel-docsite",
  "version": "1.0.0",
  "private": true,
  "type": "module",
  "scripts": {
    "docs:dev": "vitepress dev",
    "docs:build": "vitepress build",
    "docs:preview": "vitepress preview"
  },
  "devDependencies": {
    "vitepress": "^1.6.3",
    "vue": "^3.5.13"
  }
}
```

- [ ] **Step 2: 写 .gitignore**

创建 `website/.gitignore`：

```
node_modules/
.vitepress/dist/
.vitepress/cache/
```

- [ ] **Step 3: 安装依赖**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
npm install
```
Expected: 成功生成 `node_modules/` 和 `package-lock.json`，无 ERR。

- [ ] **Step 4: 提交骨架**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/package.json website/.gitignore website/package-lock.json
git commit -m "build(docsite): VitePress 骨架与依赖

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```
Expected: 提交成功（package-lock.json 入库，node_modules 被忽略）。

---

## Task 2: 站点配置 config.ts

**Files:**
- Create: `website/.vitepress/config.ts`

- [ ] **Step 1: 写 config.ts**

创建 `website/.vitepress/config.ts`：

```ts
import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'GBA 模拟器内核精讲',
  description: '基于 mGBA 源码，深入浅出讲解 GBA 模拟器内核',
  lang: 'zh-CN',
  base: '/libretro-mgba/',
  cleanUrls: true,
  appearance: 'dark',
  themeConfig: {
    nav: [
      { text: '首页', link: '/' },
      { text: '序章', link: '/guide/intro' },
    ],
    sidebar: [
      {
        text: '系列',
        items: [
          { text: '序章 · 一帧画面是怎么诞生的', link: '/guide/intro' },
        ],
      },
    ],
    search: { provider: 'local' },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/hamberluo/libretro-mgba' },
    ],
    outline: { level: [2, 3], label: '本页目录' },
    docFooter: { prev: false, next: false },
  },
})
```

- [ ] **Step 2: 验证 dev server 能起（需先有 index.md，故此处仅校验配置语法）**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
npx vitepress build 2>&1 | head -20
```
Expected: 因为还没有 index.md，可能报缺页警告，但**不应报 config.ts 语法/类型错误**。看到关于 base 或 dead link 的提示属正常。若报 `config.ts` 解析错误则修复。

- [ ] **Step 3: 提交配置**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/.vitepress/config.ts
git commit -m "build(docsite): 站点配置 config.ts（base/导航/搜索/暗色主题）

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: CPU 流水线交互组件 PipelineDemo.vue

**Files:**
- Create: `website/components/PipelineDemo.vue`
- Create: `website/.vitepress/theme/index.ts`

- [ ] **Step 1: 写 PipelineDemo.vue**

创建 `website/components/PipelineDemo.vue`：

```vue
<script setup>
import { ref, onUnmounted } from 'vue'

const stages = [
  { key: 'fetch', name: '取指 Fetch', desc: '从内存取出下一条指令（PC 指向的地址）' },
  { key: 'decode', name: '解码 Decode', desc: '解析这条指令要 CPU 做什么（mGBA 用查表法）' },
  { key: 'execute', name: '执行 Execute', desc: '真正执行：读写寄存器/内存、跳转、运算' },
]

const active = ref(-1)      // 当前高亮阶段索引，-1 表示未开始
const playing = ref(false)
let timer = null

function step() {
  active.value = (active.value + 1) % stages.length
}

function play() {
  if (playing.value) return
  playing.value = true
  if (active.value < 0) active.value = 0
  timer = setInterval(() => {
    active.value = (active.value + 1) % stages.length
  }, 1200)
}

function pause() {
  playing.value = false
  if (timer) { clearInterval(timer); timer = null }
}

function reset() {
  pause()
  active.value = -1
}

onUnmounted(() => { if (timer) clearInterval(timer) })
</script>

<template>
  <div class="pipeline">
    <div class="stages">
      <div
        v-for="(s, i) in stages"
        :key="s.key"
        class="stage"
        :class="{ active: i === active }"
      >
        <div class="stage-name">{{ s.name }}</div>
        <div class="stage-desc">{{ s.desc }}</div>
      </div>
    </div>
    <div class="controls">
      <button @click="step">单步</button>
      <button v-if="!playing" @click="play">播放</button>
      <button v-else @click="pause">暂停</button>
      <button @click="reset">重置</button>
    </div>
    <p class="hint">CPU 周而复始地重复这个循环，每秒数百万次。</p>
  </div>
</template>

<style scoped>
.pipeline {
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  padding: 1.2rem;
  margin: 1.5rem 0;
  background: var(--vp-c-bg-soft);
}
.stages {
  display: flex;
  gap: 0.8rem;
}
.stage {
  flex: 1;
  border: 2px solid var(--vp-c-divider);
  border-radius: 10px;
  padding: 0.9rem;
  transition: all 0.3s ease;
  background: var(--vp-c-bg);
}
.stage.active {
  border-color: #00d4aa;
  box-shadow: 0 0 0 3px rgba(0, 212, 170, 0.2);
  transform: translateY(-4px);
}
.stage-name {
  font-weight: 700;
  color: #00d4aa;
  margin-bottom: 0.4rem;
}
.stage-desc {
  font-size: 0.85rem;
  color: var(--vp-c-text-2);
  line-height: 1.5;
}
.controls {
  display: flex;
  gap: 0.6rem;
  margin-top: 1rem;
}
.controls button {
  padding: 0.4rem 1rem;
  border-radius: 8px;
  border: 1px solid #00d4aa;
  background: transparent;
  color: #00d4aa;
  cursor: pointer;
  font-size: 0.9rem;
  transition: background 0.2s;
}
.controls button:hover {
  background: rgba(0, 212, 170, 0.12);
}
.hint {
  margin-top: 0.8rem;
  font-size: 0.85rem;
  color: var(--vp-c-text-3);
}
@media (max-width: 640px) {
  .stages { flex-direction: column; }
}
</style>
```

- [ ] **Step 2: 写 theme/index.ts 全局注册组件**

创建 `website/.vitepress/theme/index.ts`：

```ts
import DefaultTheme from 'vitepress/theme'
import PipelineDemo from '../../components/PipelineDemo.vue'
import type { Theme } from 'vitepress'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('PipelineDemo', PipelineDemo)
  },
} satisfies Theme
```

- [ ] **Step 3: 提交组件**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/components/PipelineDemo.vue website/.vitepress/theme/index.ts
git commit -m "feat(docsite): CPU 流水线交互组件 PipelineDemo

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: 首页 index.md（10 集地图）

**Files:**
- Create: `website/index.md`

- [ ] **Step 1: 写 index.md**

创建 `website/index.md`（VitePress home layout + features 作 10 集地图；序章 link 指向 `/guide/intro`，其余 9 集为占位无 link）：

```markdown
---
layout: home
hero:
  name: GBA 模拟器内核精讲
  text: 基于 mGBA 源码，深入浅出
  tagline: 跟着「一帧画面的诞生」，看懂一台 GBA 是怎么跑起来的
  actions:
    - theme: brand
      text: 开始阅读 · 序章
      link: /guide/intro
    - theme: alt
      text: GitHub
      link: https://github.com/hamberluo/libretro-mgba
features:
  - title: 序章 · 一帧画面是怎么诞生的
    details: 全链路鸟瞰 —— 模拟器到底在模拟什么。【已上线，点上方「开始阅读」】
  - title: CPU · 软件怎么假装成一块 ARM7 芯片
    details: 取指→解码→执行。难题：解码慢？查表法。【敬请期待】
  - title: 一条指令的执行 · Thumb 指令集实战
    details: 执行→写回。难题：周期从哪来。【敬请期待】
  - title: 内存不是数组 · MMIO 与地址映射
    details: CPU 读写内存。难题：一次访存几个周期。【敬请期待】
  - title: 时间的主宰 · 周期精确与事件调度
    details: 贯穿全局的时钟。难题：怎么做到周期精确。【敬请期待】
  - title: DMA · 不打扰 CPU 的搬运工
    details: 内存→显存的高速搬运。难题：DMA 凭什么抢总线。【敬请期待】
  - title: PPU · 扫描线是怎么画出来的
    details: 显存→像素。难题：软件渲染 vs 硬件。【敬请期待】
  - title: 没有真 BIOS，游戏怎么还能跑？
    details: 启动与系统调用。难题：HLE 高级模拟。【敬请期待】
  - title: 声音 · 4+2 个声道如何合成一帧音频
    details: 像素之外的另一条线。难题：音视频同步。【敬请期待】
  - title: 随时存档读档 · 把整台机器冻在一瞬间
    details: 状态快照。难题：状态序列化的坑。【敬请期待】
---
```

- [ ] **Step 2: 验证首页能构建**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
npx vitepress build 2>&1 | tail -15
```
Expected: 此时 guide/intro 还没建，hero action 的 link 可能触发 dead link 报错。**如果构建因 dead link 失败，本步骤先不修**——Task 5 建好 intro.md 后即消失；只确认 index.md 本身 frontmatter 解析无误（无 YAML 错误）。

- [ ] **Step 3: 提交首页**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/index.md
git commit -m "feat(docsite): 首页 hero + 10 集地图

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: 序章正文 guide/intro.md

**Files:**
- Create: `website/guide/intro.md`

**内容来源：** 把 `docs/superpowers/scripts/ep01_intro/script.md` 的 7 段（段0-6）改写为**图文教程**（不是逐句旁白）。每段一个 `##` 标题。表层用比喻直觉，里层插真实源码块（带文件名）+ GitHub 跳转链接。在「一帧的旅程·上」段嵌 `<PipelineDemo />`。

- [ ] **Step 1: 写 guide/intro.md**

创建 `website/guide/intro.md`，完整内容如下：

````markdown
# 序章 · 一帧画面是怎么诞生的

你按下 A 键，屏幕上的马里奥跳了起来。整个过程快到你来不及思考——但你看到的，其实只是一帧画面。一帧只在屏幕上停留约 16 毫秒，比你眨一次眼还短得多。

就在这短短一瞬间里，机器内部发生了几百万件事：处理器在飞速地算，内存在不停地读写，有专门的部件在搬运数据，还有部件在一行一行地画像素。它们彼此配合、分秒不差，最后才凑成你眼前这一帧。

这一切到底是怎么发生的？本系列就把这一帧拆开，看看它是怎么诞生的。

## 一、什么是模拟

先问一个最朴素的问题：**模拟器到底在模拟什么？**

想象一台真正的 GBA 掌机，把它拆开，里面是一堆芯片：有负责计算的，有负责存数据的，有负责画面的，有负责声音的。它们各干各的活，又彼此协同——正是这种协同，让游戏跑了起来。

那模拟器做的事其实很简单：**用软件，把每一块芯片的行为重新复刻一遍。** 真机用电路实现的逻辑，模拟器用代码实现。我们这个系列讲的，就是 [mGBA](https://github.com/hamberluo/libretro-mgba) 这套开源内核。

在 mGBA 的源码里，整台机器就是一个结构体——一个叫 `GBA` 的结构体，把所有部件装在了一起：

```c
struct GBA {
	struct mCPUComponent d;

	struct ARMCore* cpu;        // CPU：一颗 ARM7 处理器
	struct GBAMemory memory;    // 内存
	struct GBAVideo video;      // PPU：画面
	struct GBAAudio audio;      // APU：声音
	struct GBASIO sio;          // 串口/联机

	struct mCoreSync* sync;
	struct mTiming timing;      // 时钟：统一管理时间
	// ...
};
```

> ↗ 源码：[`include/mgba/internal/gba/gba.h#L65`](https://github.com/hamberluo/libretro-mgba/blob/master/include/mgba/internal/gba/gba.h#L65)

这一集我们不深讲代码，先认认脸，记住主角是谁。

## 二、主角登场

这台机器有五大件，外加一位隐形的指挥。

- **CPU**：GBA 用的是一颗 ARM7 处理器。它负责执行游戏里的每一条指令，是整台机器的大脑。
- **内存**：游戏的代码、数据，还有正在显示的画面，都存在这里。它就像一块巨大的草稿纸，谁都能来读、来写。
- **PPU**（画面处理单元）：把内存里的数据画成一个个像素。你在屏幕上看到的一切，都出自它的手。
- **APU**（声音处理单元）：负责合成游戏的音效和音乐。
- **DMA**：一个高速搬运工，能搬运大量数据，而且不打扰 CPU 干活。

最后，是那位隐形的指挥——**一根贯穿全场的时钟**，让所有部件都对上同一个拍子。

## 三、一帧的旅程 · 上

旅程从你的手指开始。你按下 A 键，这个动作会被记录到内存里一个固定的位置，从此机器就知道：玩家按了键。

接力棒交到 CPU 手上。CPU 干活，其实是在不停地重复一个循环——**取指、解码、执行**：

<PipelineDemo />

顺着游戏的逻辑，CPU 算出了马里奥这一帧该站在哪里。算完了，结果要写回内存——它把这一帧的画面数据，写进了专门存画面的那块**显存**。

## 四、一帧的旅程 · 下

数据写进了显存，但它还只是一堆数字。要变成画面，还得有人来搬、有人来画。

这时候 **DMA** 登场了，它高速地把数据搬进画面用的缓冲区。整个过程不打扰 CPU，CPU 可以继续算下一帧。

接下来轮到 **PPU** 出场。PPU 画画的方式很有意思——它是一行一行画的。屏幕从上到下被切成很多很多条横线，每一条叫做一条**扫描线**。PPU 从最上面那一行开始画起，画完一行往下挪一行，再画下一行，一直画到屏幕最底下那一行。当最后一行画完，一整帧就完整地亮了起来。

与此同时，APU 也合成好了这一帧该有的声音。

## 五、隐形的主宰

到这里你可能会冒出一个问题：这么多部件各干各的，凭什么能对得这么齐？

答案就是刚才那位隐形的指挥——**那根贯穿全场的时钟**。在这台机器里，每个部件做每一件事，都要花掉确定数量的时钟周期：取一条指令、搬一批数据、画一行像素，各有各的耗时。时钟一拍一拍地走，谁也快不了，谁也慢不了。

mGBA 是怎么管住这一切的呢？它用了一个**事件调度器**，专门统一管理时间：

```c
void mTimingSchedule(struct mTiming* timing,
                     struct mTimingEvent* event,
                     int32_t when);
```

> ↗ 源码：[`src/core/timing.c#L36`](https://github.com/hamberluo/libretro-mgba/blob/master/src/core/timing.c#L36)

谁该在第几个周期做哪件事，都被安排得明明白白。正是这套安排，让所有部件严丝合缝地对上了拍子。而这，恰恰就是写一个模拟器最核心的难题：**周期精确**。

## 六、系列地图

把刚才这趟旅程画成一张地图：按键、CPU、内存、DMA、PPU、APU，还有那根贯穿全场的时钟——这张地图就是我们整个系列的骨架。往后的每一集，都会停在地图上的某一站，往里深挖。

下一集，我们就从 CPU 开始，聊聊一个最根本的问题：**软件，到底怎么假装成一块芯片。**

一帧画面的诞生，今天我们只是看了个轮廓。这趟旅程，才刚刚开始。
````

- [ ] **Step 2: 本地起 dev server 验证渲染与交互**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
npx vitepress dev --port 5180 &
sleep 6
curl -s http://localhost:5180/libretro-mgba/guide/intro.html -o /dev/null -w "%{http_code}\n"
```
Expected: 返回 `200`。然后人工/截图确认序章页：标题、6 个二级标题、两段代码块带高亮、两个 GitHub 链接、`<PipelineDemo />` 渲染成三阶段卡片。验证后停掉 dev server（`kill %1` 或找到进程结束）。

- [ ] **Step 3: 全量构建验证（dead link 此时应消失）**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
npx vitepress build 2>&1 | tail -15
```
Expected: 构建成功，输出 `build complete` 类信息，无 dead link 报错（intro.md 已建，首页 action 链接有效）。生成 `.vitepress/dist/`。

- [ ] **Step 4: 提交序章**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/guide/intro.md
git commit -m "content(docsite): 序章正文（图文 + 真实源码 + GitHub 跳转 + 流水线组件）

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: GitHub Actions 部署 workflow

**Files:**
- Create: `.github/workflows/docs.yml`

- [ ] **Step 1: 写 docs.yml**

创建 `.github/workflows/docs.yml`（仅当 website/ 变更时触发，构建并部署到 Pages）：

```yaml
name: Deploy Docs to GitHub Pages

on:
  push:
    branches: [master]
    paths:
      - 'website/**'
      - '.github/workflows/docs.yml'
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

concurrency:
  group: pages
  cancel-in-progress: false

jobs:
  build:
    runs-on: ubuntu-latest
    defaults:
      run:
        working-directory: website
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 20
      - run: npm ci
      - run: npm run docs:build
      - uses: actions/configure-pages@v5
      - uses: actions/upload-pages-artifact@v3
        with:
          path: website/.vitepress/dist

  deploy:
    needs: build
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - id: deployment
        uses: actions/deploy-pages@v4
```

说明：build job 用 `working-directory: website` 跑 npm；upload 路径用相对仓库根的 `website/.vitepress/dist`（upload-pages-artifact 的 path 相对仓库根，不受 working-directory 影响）。node 用 20（Actions 稳定 LTS，本地 v25 仅开发用）。

- [ ] **Step 2: 校验 yml 语法**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/docs.yml')); print('yaml ok')"
```
Expected: 打印 `yaml ok`（若本机无 pyyaml，改用 `npx --yes yaml-lint .github/workflows/docs.yml` 或跳过，靠 push 后 Actions 报告）。

- [ ] **Step 3: 提交 workflow**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add .github/workflows/docs.yml
git commit -m "ci(docsite): GitHub Actions 部署 VitePress 到 Pages

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: 部署与线上验证（需用户操作 GitHub 设置）

**Files:** 无（仓库设置 + push）

- [ ] **Step 1: 提示用户启用 Pages 的 Actions 来源**

告知用户：到 GitHub 仓库 `Settings → Pages → Build and deployment → Source` 选择 **GitHub Actions**（不是 Deploy from a branch）。这是 workflow 能部署的前提，必须人工设置一次。

- [ ] **Step 2: push 触发部署**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git push origin master
```
Expected: push 成功。`paths` 过滤命中 website/ 变更，触发 docs workflow。

- [ ] **Step 3: 观察 Actions 运行**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
gh run list --workflow=docs.yml --limit 3
```
Expected: 看到一条 docs workflow 运行记录。用 `gh run watch <run-id>` 跟踪到 completed/success。若失败，`gh run view <run-id> --log-failed` 看日志修复。

- [ ] **Step 4: 线上验证**

```bash
curl -s -o /dev/null -w "%{http_code}\n" https://hamberluo.github.io/libretro-mgba/
curl -s -o /dev/null -w "%{http_code}\n" https://hamberluo.github.io/libretro-mgba/guide/intro.html
```
Expected: 均返回 `200`（Pages 首次部署可能有几分钟延迟）。然后浏览器打开 `https://hamberluo.github.io/libretro-mgba/`，确认：首页 10 集地图、序章可点进、源码链接跳 GitHub 正确行、PipelineDemo 可单步/播放、暗色主题、子路径资源无 404。

---

## Task 8: README 指引（可选收尾）

**Files:**
- Create: `website/README.md`

- [ ] **Step 1: 写 website/README.md**

```markdown
# GBA 内核精讲文档站

VitePress 静态文档站，部署在 https://hamberluo.github.io/libretro-mgba/

## 本地开发

```bash
cd website
npm install
npm run docs:dev      # 本地预览 http://localhost:5173/libretro-mgba/
npm run docs:build    # 构建到 .vitepress/dist
```

## 结构
- `index.md` — 首页（10 集地图）
- `guide/` — 各集正文
- `components/` — 交互组件（Vue SFC）
- `.vitepress/config.ts` — 站点配置

push 到 master 且改动 `website/**` 时，GitHub Actions 自动部署。
```

- [ ] **Step 2: 提交并 push**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/README.md
git commit -m "docs(docsite): website 本地开发说明

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git push origin master
```
Expected: 提交并推送成功。
