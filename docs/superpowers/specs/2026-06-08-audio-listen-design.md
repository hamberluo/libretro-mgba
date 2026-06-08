# 全站「听书」功能设计：每章音频朗读

> 给 VitePress 文档站 10 篇正文各加一个音频听书播放器。
> 日期：2026-06-08 ｜ 作者：Hamber（自主执行，已获全权授权）

## 1. 背景与目标

硬核内容配「听书」适合通勤/碎片时间过一遍思路。给每篇正文顶部加一个音频播放器条，点开就能听本篇的讲解（散文部分），代码细节引导回网页深读。

## 2. 决策汇总（已定）

- **音频来源**：edge-tts 预生成 mp3，音色 `zh-CN-XiaoxiaoNeural`（与视频系列一致），语速 `+30%`（听书比视频稍慢，更适合理解；视频是 +50%）。
- **朗读内容**：散文照念；代码块替换为「这里有一段源码，详见网页」；交互组件替换为「这里有一个交互演示，可以在网页上动手试试」；源码引用块（`> ↗ 源码`）跳过；标题正常念。
- **粒度/形态**：每篇 1 个 mp3；正文顶部（一级标题下方）放一个播放器条：播放/暂停、进度条、当前/总时长、倍速（0.75 / 1 / 1.25 / 1.5）。无逐句高亮跟读（YAGNI）。
- **存放**：`website/public/audio/<slug>.mp3` 进 git（实测全系列 ~12-18 MB，可接受）。VitePress 的 `public/` 会原样拷到站点根，运行时路径为 `/libretro-mgba/audio/<slug>.mp3`（注意 base 前缀）。
- **生成**：本地脚本 `tools/build-listen-audio.mjs`（或 .py）读 `guide/*.md` → 清洗成朗读文本 → edge-tts 合成 → 写入 `public/audio/`。本地跑一次、产物提交。正文更新后手动重跑对应篇。

## 3. 架构与组件

### 3.1 音频生成脚本 `website/tools/build-listen-audio.py`
- 输入：`website/guide/*.md`（10 篇）。
- 清洗规则（markdown → 朗读纯文本）：
  1. 去掉 frontmatter（本项目 guide 无 frontmatter，跳过即可）。
  2. ` ```c … ``` ` 代码块整段 → 替换为一句「这里有一段源码，详见网页。」
  3. `<XxxDemo />` 组件标签行 → 替换为「这里有一个交互演示，可以在网页上动手试试。」
  4. `> ↗ 源码…` 引用块行 → 删除（不念链接）。
  5. markdown 表格行（`| … |`）→ 删除（表格不适合念）。
  6. 标题 `#`/`##` → 去掉井号，保留文字（作为一句念，前后留停顿）。
  7. 行内 markdown：去掉 `**` `` ` `` `[]()` 等标记，保留文字。
  8. 多空行压成段落停顿。
- 输出：`website/public/audio/<slug>.mp3`，slug 对应文件名（intro, ep02-cpu, …, ep10-savestate）。
- 参数：`VOICE='zh-CN-XiaoxiaoNeural'`、`RATE='+30%'`。
- 幂等：可重复跑；支持只生成指定篇（命令行传 slug，便于正文更新后单篇重跑）。

### 3.2 播放器组件 `website/components/AudioPlayer.vue`
- props：`src`（音频路径，相对站点根，如 `/libretro-mgba/audio/ep05-timing.mp3`）。
- 用原生 `<audio>` 元素 + 自定义控制 UI（不暴露默认 controls，保持风格统一）。
- UI：播放/暂停按钮、进度条（可点击 seek）、当前时间 / 总时长、倍速切换（0.75/1/1.25/1.5，点击循环或下拉）。
- 纯 Vue3 + CSS，青绿暗色，遵循统一规范（按钮 hover 0.12、数字 nowrap）。
- 顶部一行紧凑布局，不喧宾夺主（高度约一行控件）。
- 全局注册（theme/index.ts），各 md 用 `<AudioPlayer src="..." />`。

### 3.3 每篇接入
- 在每个 `guide/*.md` 的一级标题之后、正文开始之前，插入一行：
  `<AudioPlayer src="/libretro-mgba/audio/<slug>.mp3" />`
- slug 与该篇文件名一致。

## 4. base 前缀注意

VitePress base 是 `/libretro-mgba/`。`public/audio/x.mp3` 在运行时是 `/libretro-mgba/audio/x.mp3`。组件 src 必须带这个前缀（VitePress 不会对组件内的字符串 src 自动加 base）。为稳妥，AudioPlayer 内部用 `import { withBase } from 'vitepress'` 包一下 src，或各 md 直接写全 `/libretro-mgba/audio/...`。**采用 withBase 方案**：md 里只传 `audio/<slug>.mp3`，组件内 `withBase(src)`，避免硬编码 base、未来改 base 不破。

## 5. 站点改动

- 新增 `website/tools/build-listen-audio.py`。
- 新增 `website/public/audio/intro.mp3` … `ep10-savestate.mp3`（10 个）。
- 新增 `website/components/AudioPlayer.vue` + theme/index.ts 注册（第 11 个组件）。
- 10 个 `guide/*.md` 各加一行 `<AudioPlayer src="audio/<slug>.mp3" />`。
- `.gitignore` 不忽略 public/audio（要进 git）。

## 6. 不做什么（YAGNI）

- 不做逐句高亮跟读 / 字幕同步（需逐句时间戳，工程量大、网页场景收益低）。
- 不做分段音频（每篇一条整的）。
- 不做播放列表 / 连续播放下一篇。
- 不在 CI 自动生成（本地脚本一次性生成，正文改了手动重跑该篇）。
- 不做下载按钮、倍速记忆持久化等附加项（先上核心）。

## 7. 验收标准

1. `build-listen-audio.py` 能把 10 篇 md 清洗成朗读文本（代码块/组件/源码引用/表格按规则处理）并生成 10 个 mp3 到 public/audio/。
2. 抽听 1-2 篇 mp3：念的是散文，代码块处念「这里有一段源码」、组件处念「交互演示」，无念代码/链接的拗口内容。
3. `<AudioPlayer />` 在页面顶部渲染：可播放/暂停、进度条 seek、显示时长、切倍速；暗色青绿风格、无溢出。
4. 10 篇都接入了播放器，src 经 withBase 正确指向 `/libretro-mgba/audio/<slug>.mp3`，线上能播。
5. build 通过无死链；push 后线上每篇顶部都有可用播放器。

## 8. 下一步

writing-plans → subagent 执行（生成脚本 → 跑出 mp3 → 播放器组件 → 10 篇接入 → 构建验证）→ 部署 → 抽听+截图自查 → 向 Hamber 汇报。
