# -*- coding: utf-8 -*-
"""manim 场景模板 — 按 timeline.json 逐句驱动，音画绝对时间轴对齐，无字幕。

用法：
  1. 把本文件复制为 scene.py，与 timeline.json / voice.mp3 同目录。
  2. 【时间轴机制】区块（P / W / sync_to / beat）原样保留，不要改。
  3. 把 construct() 里的场景列表换成你自己的；每个场景里：每句一个 self.beat()。
  4. beat() 的数量必须 == 口播稿（script.md）的句子总数，且顺序一致。
  5. 渲染：python3 -m manim -ql --disable_caching scene.py MyVideo   （先低质量验证）
          python3 -m manim -qh --fps 60 --disable_caching scene.py MyVideo  （出 1080p60）
"""
import json
from pathlib import Path
from manim import *

WORK = Path(__file__).resolve().parent
TL = json.loads((WORK / "timeline.json").read_text(encoding="utf-8"))
SENTS = TL["sentences"]
GAP = TL.get("gap", 0.30)

# ---- 配色 & 字体（按需改）----
CN = "Heiti SC"          # 中文字体；非 macOS 用 `fc-list :lang=zh` 查系统字体名
BG = "#0d1b2a"           # 背景
ACCENT = "#00d4aa"       # 主强调（青绿）
GOLD = "#ffd166"         # 高亮
BLUE = "#5b8def"
WARN = "#ff6b6b"         # 警示/拒绝
GREY = "#8d99ae"
config.background_color = BG


class Ep01Intro(Scene):
    def construct(self):
        self.cursor = 0
        self.t = 0.0
        card = self.code_card(
            ["struct GBA {", "  struct ARMCore* cpu;", "  struct GBAVideo video;", "};"],
            title="src/.../gba.h", hl={1})
        nodes, arrows = self.flow_pipeline(
            ["按键", "CPU", "显存", "PPU", "屏幕"],
            [GREY, ACCENT, BLUE, GOLD, WARN], y=-2.2)
        self.add(card.shift(UP * 1.5), nodes, arrows)
        self.wait(1)

    # ========================================================================
    # 【时间轴机制】—— 原样保留，勿改。这是音画对齐的核心。
    # ========================================================================
    def P(self, *anims, run_time=1.0):
        """带计时的 play：所有动画播放都走这里，累计绝对时间。"""
        self.play(*anims, run_time=run_time)
        self.t += run_time

    def W(self, dt):
        """带计时的 wait。"""
        if dt > 0.02:
            self.wait(dt)
            self.t += dt

    def sync_to(self, target):
        """把时间轴补齐到 target 秒（吸收一切误差，永不累积）。"""
        self.W(target - self.t)

    def beat(self, intro_anims=None, intro_t=0.6):
        """播一句：在该句音频窗口内播完入场动画，再补齐到下一句开始。
        - 纯停顿的句子：beat() 不传参数。
        - 带画面的句子：beat(intro_anims=[...], intro_t=动画时长上限)。
        """
        s = SENTS[self.cursor]
        win = SENTS[self.cursor + 1]["start"] if self.cursor + 1 < len(SENTS) else s["end"] + GAP
        if intro_anims:
            t = min(intro_t, max(0.3, (win - self.t) - 0.2))
            self.P(*intro_anims, run_time=t)
        self.sync_to(win)
        self.cursor += 1

    # ========================================================================
    # 【通用工具】
    # ========================================================================
    def node(self, label, color=WHITE, w=2.6, h=0.9, fs=20, fill=0.12):
        """流程图节点：圆角框 + 居中文字（自动缩放防溢出）。"""
        box = RoundedRectangle(width=w, height=h, corner_radius=0.15, color=color,
                               stroke_width=2.5, fill_color=color, fill_opacity=fill)
        txt = Text(label, font=CN, font_size=fs, color=WHITE, line_spacing=0.75)
        if txt.width > w - 0.3:
            txt.scale_to_fit_width(w - 0.3)
        txt.move_to(box.get_center())
        return VGroup(box, txt)

    def head(self, text):
        """场景标题：固定顶部居中。"""
        return Text(text, font=CN, font_size=28, color=GOLD, weight=BOLD).to_edge(UP, buff=0.5)

    def code_card(self, lines, title="", w=8.0, fs=22, hl=None):
        """代码特写卡片：深色圆角面板 + 等宽代码 + 可选标题/高亮行。
        lines: 代码字符串列表；hl: 需高亮的行号集合(0-based)。返回 VGroup。"""
        hl = hl or set()
        code_lines = VGroup()
        for i, ln in enumerate(lines):
            c = GOLD if i in hl else "#d7e3fc"
            t = Text(ln if ln else " ", font="Menlo", font_size=fs, color=c)
            code_lines.add(t)
        code_lines.arrange(DOWN, aligned_edge=LEFT, buff=0.16)
        panel = RoundedRectangle(width=w, height=code_lines.height + 0.9,
                                 corner_radius=0.18, color=GREY,
                                 stroke_width=2, fill_color="#10243a", fill_opacity=0.95)
        code_lines.move_to(panel.get_center())
        grp = VGroup(panel, code_lines)
        if title:
            cap = Text(title, font="Menlo", font_size=fs - 4, color=GREY)
            cap.next_to(panel, UP, buff=0.15).align_to(panel, LEFT)
            grp.add(cap)
        return grp

    def flow_pipeline(self, labels, colors, y=0.0, w=2.2, h=0.9, gap=0.5):
        """水平数据流：一排节点 + 节点间箭头。返回 (nodes:VGroup, arrows:VGroup)。
        逐站高亮时对 nodes[i] 做 indicate / 改 stroke。"""
        nodes = VGroup()
        for lbl, col in zip(labels, colors):
            nodes.add(self.node(lbl, col, w=w, h=h, fs=18))
        nodes.arrange(RIGHT, buff=gap).move_to(UP * y)
        arrows = VGroup()
        for i in range(len(nodes) - 1):
            arrows.add(Arrow(nodes[i].get_right(), nodes[i + 1].get_left(),
                             color=GREY, buff=0.08, stroke_width=3, max_tip_length_to_length_ratio=0.2))
        return nodes, arrows

    # ========================================================================
    # 【你的场景】—— 把下面换成自己的内容。注意 beat() 总数 == 口播稿句数。
    # ========================================================================
    # （正片 7 段内容由下一个 Task 填充）
