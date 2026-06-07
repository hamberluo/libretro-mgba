# Pilot《序章》复盘与风格定调（系列共用基线）

> 后续每一集的 spec/plan 都应读本文件作为输入。本集已跑通整条产线，以下结论作为系列基线。

## 一、产线（已验证可用）

复用 `~/.claude/skills/tools/doc_to_video` 四步：
1. 写 `script.md`（`## 段名` + `- 句子`，一句一行）
2. `python3 build_audio.py` → `voice.mp3` + `timeline.json`（每句精确起止）
3. `scene.py` 用 `P/W/sync_to/beat` 按绝对时间轴对齐，`beat()` 总数 == 句数
4. `-qh --fps 60` 渲染 → ffmpeg 合并配音轨出成片

每集独立工作目录：`~/Documents/personal/gba_kernel_video/epNN_xxx/`。
成片/media/voice 等大文件**不入 git**；仅 script.md / scene.py / NOTES.md 入仓库 `docs/superpowers/scripts/epNN_xxx/`。

## 二、时长公式（重要，规划时必须用）

**+50% 语速（1.5 倍速）下，约 3 句/分钟有效信息密度下：92 句 = 4 分 40 秒。**
- 经验值：**约 20 句 ≈ 1 分钟**。
- 序章这种轻快开篇 ~5 分钟（90-100 句）合适。
- 若要做 ~14 分钟深度集，需 **~250-300 句**，且必然要包含代码逐段讲解（不是纯比喻）。
- 规划单集时先按目标分钟数 ×20 估句数，再分配到各段。

## 三、视觉风格基线（频道统一）

- 画幅：16:9，1080p / 60fps。
- 背景 `BG=#0d1b2a`（深蓝）。
- 主强调 `ACCENT=#00d4aa`（青绿）、高亮 `GOLD=#ffd166`、`BLUE=#5b8def`、警示 `WARN=#ff6b6b`、`GREY=#8d99ae`。
- 中文字体 `Heiti SC`；代码等宽字体 `Menlo`（macOS 自带，已验证）。
- 配音 `zh-CN-XiaoxiaoNeural`，语速 `+50%`，句间 0.30s，无底部字幕。

## 四、两个复用模板（已在 scene.py 中沉淀，后续集直接复用）

### `code_card(lines, title="", w=8.0, fs=22, hl=None)`
代码特写卡片：深色面板 + Menlo 等宽代码 + 可选标题/高亮行（`hl` 是 0-based 行号集合）。
- **缩进坑（已修）**：manim 0.19 的 `Text` 会 trim 行首空白，全角空格 U+3000 也会被忽略。`code_card` 内部已做修复——剥离行首空格转成缩进级数，arrange 后按级数手动右移。传入带前导空格的代码行即可正常显示缩进。
- `card[0]` = 面板，`card[1]` = code_lines VGroup（`card[1][i]` 是第 i 行 Text，可单独 Indicate/set_color 做高亮），`card[2]` = 标题（若有）。

### `flow_pipeline(labels, colors, y=0.0, w=2.2, h=0.9, gap=0.5)`
水平数据流：一排 `node()` + 节点间箭头。返回 `(nodes, arrows)`。逐站推进时对 `nodes[i]` 做 `Indicate`、对 `arrows[i]` 做 `GrowArrow`。

> 建议：第 2 集 spec 时把这两个方法 + 配色常量 + 时间轴机制抽成 `base_scene.py`，各集 `class EpNN(BaseScene)` 继承，避免每集复制粘贴。本集未抽离（Pilot 先跑通为先）。

## 五、双层结构的画面落地法（本集验证有效）

「表层 + 里层」同屏并置是有效手法：seg1 左侧真机芯片图（比喻/表层）↔ 右侧真实 `struct GBA` 代码卡（源码/里层），中间箭头连接。后续集可沿用「比喻图 ↔ 真实源码卡」这个对照范式。

## 六、本集实测数据

- script.md：7 段 92 句；配音 280.6s。
- 成片 序章.mp4：1920x1080@60fps，280.3s，h264+aac，10.5MB，末句完整未截。
- beat 92 == 句数 92，音画零累积误差（视音差 0.04s）。

## 七、待办 / 下一集输入

- 第 2 集《CPU：软件怎么假装成一块 ARM7 芯片》：源码锚点 `src/arm/arm.c`、`decoder-arm.c`、`decoder-thumb.c`。是主线最硬部分，需含真实指令解码代码逐段讲，预计句数应显著高于序章（奔着 8-12 分钟去，约 180-240 句）。
- 抽 `base_scene.py` 的工作放在第 2 集 spec 阶段做。
