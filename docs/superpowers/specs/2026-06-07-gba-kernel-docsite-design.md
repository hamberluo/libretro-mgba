# GBA 模拟器内核精讲 · VitePress 文档站设计

> 把《GBA 模拟器内核源码精讲》系列从「视频」转向「静态文档站」的设计。
> 日期：2026-06-07 ｜ 作者：Hamber

## 1. 背景与转向

原计划做 manim 视频系列（Pilot《序章》成片已完成，见
`docs/superpowers/scripts/ep01_intro/`）。现转向**静态文档站**：源码精讲做成可读、可跳转、可搜索的网页，更适合「对着代码随时查」的硬核内容。

已有内容资产可无损迁移：系列 10 集大纲（见
`2026-06-07-gba-emulator-kernel-series-design.md`）、序章 92 句逐段讲解稿
（`docs/superpowers/scripts/ep01_intro/script.md`）、已核实的真实源码锚点。

**核心调性沿用视频系列**：深入浅出 · 双层结构（表层比喻 + 里层源码）；主线「一帧画面的诞生」+ 经典难题钩子。

## 2. 形态决策

- **方向**：A（静态文档站）为主 + C（关键处交互动画）点缀。
- **工具**：**VitePress**。Vue 组件可直接嵌入 markdown 做交互动画，内置搜索/代码高亮/暗黑主题，node v25 现成。
- **v1 范围**：搭站骨架 + 迁移《序章》一篇 + 1 个 CPU 流水线交互组件（样板）+ 部署 workflow 打通。等于「Pilot 的网页版」，验证形态后再批量写后 9 篇。

## 3. 目录与部署

- **站点目录**：仓库根下新建 `website/`，与 `docs/superpowers/`（规划产物）完全分开，边界清晰。
- **部署 URL**：`https://hamberluo.github.io/libretro-mgba/`（项目站，带子路径）。
- **VitePress base**：`/libretro-mgba/`（项目站子路径必须配，否则资源 404）。
- **部署方式**：GitHub Actions workflow（`.github/workflows/docs.yml`）在 push 到 master 时构建 `website/` 并发布到 GitHub Pages。仓库已有 3 个编译相关 workflow，本 workflow 独立、互不干扰。

### 目录结构（v1）

```
website/
  package.json                 # vitepress 依赖与脚本
  .vitepress/
    config.ts                  # 站点配置：标题/base/导航/侧边栏/搜索/主题
    theme/
      index.ts                 # 注册全局交互组件
  index.md                     # 首页（hero + 系列简介 + 10 集地图）
  guide/
    intro.md                   # 序章正文（由 script.md 改写为图文阅读版）
  components/
    PipelineDemo.vue           # CPU 取指-解码-执行 流水线交互动画（C）
  public/                      # 静态资源（图等，按需）
```

> `docs/superpowers/` 不在 `website/` 内，VitePress 不会扫描到，零干扰。

## 4. 内容设计（v1 只做序章）

### 4.1 首页 `index.md`
- VitePress 默认 home layout：hero（标题「GBA 模拟器内核源码精讲」+ 一句定位 + 「开始阅读」按钮跳序章）。
- 下方 features 区放 10 集地图：每集一张卡片（标题 + 钩子难题 + 主线位置），已完成的可点进，未完成的标「敬请期待」。地图顺序沿用系列大纲。

### 4.2 序章正文 `guide/intro.md`
把 script.md 的 7 段 92 句**改写为适合阅读的图文**（不是逐句念稿，而是图文教程）：
- 段0-6 对应 7 个二级标题。
- 表层：比喻、直觉描述（草稿纸、搬运工、扫描线）。
- 里层：真实源码片段，用 VitePress 代码块（带语法高亮、行号、文件名）。关键代码块下方给「↗ 在 GitHub 查看源码」链接，指向具体文件行（如 `include/mgba/internal/gba/gba.h#L65`）。
- 在「一帧的旅程·上」（CPU 取指-解码-执行）这一段嵌入交互组件 `<PipelineDemo />`。
- 末尾「下一集预告」链接（CPU 篇，v1 暂为占位）。

### 4.3 真实源码锚点（已核实，文中引用）
- `struct GBA`：`include/mgba/internal/gba/gba.h:65`，开头成员 cpu/memory/video/audio/sio/timing。
- 事件调度：`src/core/timing.c`（`mTimingSchedule` 等）。
- CPU 核：`src/arm/arm.c`、`decoder-arm.c`、`decoder-thumb.c`。
- PPU：`src/gba/video.c`、`src/gba/renderers/`。

## 5. 交互组件设计 `PipelineDemo.vue`（C 的样板）

**目的**：把 CPU「取指 → 解码 → 执行」循环做成可交互的动画，替代视频里的流水线动画。这是 v1 唯一的交互组件，作为后续所有交互的模板。

**形态**：
- 横向三个阶段框：取指(Fetch) / 解码(Decode) / 执行(Execute)。
- 一个「指令」方块随「单步 / 播放 / 重置」按钮在三阶段间流动，当前阶段高亮。
- 每个阶段下方一行说明文字（这一步在 mGBA 里对应做什么）。
- 纯 Vue + CSS transition 实现，不引第三方动画库（YAGNI）；配色沿用视频系列基线（深蓝底 `#0d1b2a`、青绿 `#00d4aa`、金 `#ffd166`）。
- 响应式：窄屏（手机）三阶段竖排。

**接口**：无 props，自包含。`<PipelineDemo />` 直接用。后续若要参数化（不同指令集）再加 props。

## 6. 视觉风格

沿用视频系列频道基线，保持品牌统一：
- 主题色：青绿 `#00d4aa`（VitePress `--vp-c-brand`）。
- 暗色为默认主题（深蓝底契合代码阅读）。
- 中文字体走系统默认（VitePress 自带的字体栈对中文友好），不强依赖 Heiti SC。

## 7. 不做什么（YAGNI）

- v1 不写第 2-10 集正文（只在首页地图留占位）。
- 不做 i18n、版本化、评论系统、博客。
- 不引重型动画库（Three.js/GSAP 等）；交互用原生 Vue + CSS。
- 不做自定义域名（用默认 github.io 子路径）。
- 视频成片产物（序章.mp4 等）不迁移，留在原工作目录；文档站是独立的新形态。

## 8. 验收标准（v1）

1. `cd website && npm run docs:dev` 本地能起站，序章页正常渲染、`<PipelineDemo />` 可交互。
2. `npm run docs:build` 构建成功，无报错。
3. push master 后 GitHub Actions 自动部署，`https://hamberluo.github.io/libretro-mgba/` 可访问，子路径资源不 404。
4. 序章页：双层结构呈现（比喻 + 真实源码块 + GitHub 源码跳转链接可点）、流水线交互组件能单步/播放。
5. 首页 10 集地图展示，序章可点进、其余标占位。

## 9. 下一步

本设计通过后 → writing-plans 产出 v1 实现计划（搭骨架 → 配置 → 序章正文 → 交互组件 → 本地验证 → 部署 workflow → 线上验证）。
