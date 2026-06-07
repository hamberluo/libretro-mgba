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
        self.seg0_hook()
        self.seg1_what_is_emulation()
        self.seg2_cast()
        self.seg3_journey_a()
        self.seg4_journey_b()
        self.seg5_clock()
        self.seg6_map()

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
        # manim 的 Text 不为行首空白生成 glyph，导致缩进丢失；
        # 这里把行首全角/半角空格剥离成缩进级数，arrange 后再手动右移。
        code_lines = VGroup()
        indents = []
        for i, ln in enumerate(lines):
            stripped = ln.lstrip("　 ")
            indents.append(len(ln) - len(stripped))
            c = GOLD if i in hl else "#d7e3fc"
            t = Text(stripped if stripped else " ", font="Menlo", font_size=fs, color=c)
            code_lines.add(t)
        code_lines.arrange(DOWN, aligned_edge=LEFT, buff=0.16)
        unit = fs * 0.018  # 每个行首空格的缩进宽度（随字号缩放）
        for t, ind in zip(code_lines, indents):
            if ind:
                t.shift(RIGHT * unit * ind)
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
    # ------------------------------------------------------------------
    # 段0 开场钩子 — 12 句
    # ------------------------------------------------------------------
    def seg0_hook(self):
        # 画面定格框
        frame = RoundedRectangle(width=5.2, height=3.4, corner_radius=0.2,
                                 color=ACCENT, stroke_width=3,
                                 fill_color="#10243a", fill_opacity=0.9)
        mario = Text("A", font=CN, font_size=70, color=GOLD, weight=BOLD).move_to(frame)
        ms = Text("16 ms", font="Menlo", font_size=46, color=WARN, weight=BOLD)
        title = self.head("一帧画面，是怎么诞生的")

        # 1 你按下A键马里奥跳起
        self.beat([FadeIn(frame, scale=0.9), Write(mario)], intro_t=0.7)
        # 2 快到来不及思考
        self.beat([Indicate(mario, color=GOLD, scale_factor=1.4)], intro_t=0.5)
        # 3 其实只是一帧画面
        self.beat([Circumscribe(frame, color=ACCENT)], intro_t=0.7)
        # 4 一帧只停留十六毫秒
        self.beat([mario.animate.set_opacity(0.2), FadeIn(ms.next_to(frame, DOWN, buff=0.4), shift=UP)], intro_t=0.6)
        # 5 比眨眼还短
        self.beat([Indicate(ms, color=WARN, scale_factor=1.3)], intro_t=0.5)
        # 6 这一瞬机器发生几百万件事
        dots = VGroup(*[Dot(radius=0.05, color=BLUE) for _ in range(24)])
        for d in dots:
            d.move_to(frame.get_center() + np.array([np.random.uniform(-2, 2), np.random.uniform(-1.3, 1.3), 0]))
        self.beat([LaggedStart(*[FadeIn(d, scale=0.3) for d in dots], lag_ratio=0.04)], intro_t=0.7)
        # 7 处理器飞速算内存不停读写
        self.beat([LaggedStart(*[Flash(d, color=ACCENT, line_length=0.12) for d in dots[:12]], lag_ratio=0.03)], intro_t=0.6)
        # 8 有部件搬数据有部件画像素
        self.beat([LaggedStart(*[Flash(d, color=GOLD, line_length=0.12) for d in dots[12:]], lag_ratio=0.03)], intro_t=0.6)
        # 9 彼此配合分秒不差
        self.beat([dots.animate.set_color(ACCENT)], intro_t=0.5)
        # 10 最后凑成这一帧
        self.beat([FadeOut(dots), mario.animate.set_opacity(1.0), Indicate(frame, color=GOLD)], intro_t=0.7)
        # 11 这一切怎么发生的
        q = Text("?", font=CN, font_size=60, color=GOLD, weight=BOLD).next_to(frame, RIGHT, buff=0.6)
        self.beat([Write(q)], intro_t=0.5)
        # 12 今天把这一帧拆开看怎么诞生
        self.beat([FadeOut(q), Circumscribe(title, color=GOLD)], intro_t=0.6)

        self.P(FadeOut(frame, mario, ms, title), run_time=0.5)

    # ------------------------------------------------------------------
    # 段1 什么是模拟 — 15 句
    # ------------------------------------------------------------------
    def seg1_what_is_emulation(self):
        title = self.head("模拟器在模拟什么")
        # 左侧真机芯片图
        chips = VGroup(
            self.node("ARM7 处理器", ACCENT, w=2.4, h=0.7, fs=16),
            self.node("内存", BLUE, w=2.4, h=0.7, fs=16),
            self.node("画面 / 声音", GOLD, w=2.4, h=0.7, fs=16),
        ).arrange(DOWN, buff=0.3)
        board = RoundedRectangle(width=3.1, height=3.4, corner_radius=0.2,
                                 color=GREY, stroke_width=2.5,
                                 fill_color="#10243a", fill_opacity=0.6)
        chips.move_to(board)
        left = VGroup(board, chips).to_edge(LEFT, buff=1.0).shift(DOWN * 0.3)
        lcap = Text("真机：电路", font=CN, font_size=18, color=GREY).next_to(left, UP, buff=0.15)

        card = self.code_card(
            ["struct GBA {",
             "　　struct ARMCore* cpu;",
             "　　struct GBAMemory memory;",
             "　　struct GBAVideo video;",
             "　　struct GBAAudio audio;",
             "　　struct mTiming timing;",
             "};"],
            title="include/.../gba.h", w=6.0, fs=20)
        card.to_edge(RIGHT, buff=0.8).shift(DOWN * 0.3)
        lines = card[1]  # code_lines VGroup, idx 1..5 = cpu/memory/video/audio/timing

        # 1 先问朴素问题
        self.beat([Write(title)], intro_t=0.6)
        # 2 模拟器在模拟什么
        self.beat([Indicate(title, color=GOLD, scale_factor=1.05)], intro_t=0.5)
        # 3 想象真正的GBA掌机
        self.beat([FadeIn(board), FadeIn(lcap)], intro_t=0.6)
        # 4 拆开里面一堆芯片
        self.beat([LaggedStart(*[FadeIn(c, shift=RIGHT * 0.3) for c in chips], lag_ratio=0.3)], intro_t=0.7)
        # 5 有计算存数据画面声音的
        self.beat([LaggedStart(*[Indicate(c, color=GOLD) for c in chips], lag_ratio=0.2)], intro_t=0.6)
        # 6 各干各活又协同
        self.beat([chips.animate.set_opacity(0.85)], intro_t=0.4)
        # 7 协同让游戏跑起来
        self.beat([Circumscribe(left, color=ACCENT)], intro_t=0.6)
        # 8 模拟器做的事很简单
        self.beat([FadeIn(card[0]), FadeIn(card[2] if len(card) > 2 else card[1])], intro_t=0.6)
        # 9 用软件复刻每块芯片行为
        self.beat([Write(lines[0]), Write(lines[6])], intro_t=0.6)
        # 10 真机用电路模拟器用代码
        arrow = Arrow(left.get_right(), card.get_left(), color=GREY, buff=0.2, stroke_width=3)
        self.beat([GrowArrow(arrow)], intro_t=0.5)
        # 11 我们讲mGBA这套开源内核
        self.beat([Indicate(card[0], color=ACCENT)], intro_t=0.5)
        # 12 源码里整台机器就是一个结构体
        self.beat([Circumscribe(card[0], color=GOLD)], intro_t=0.5)
        # 13 一个叫GBA的结构体装下所有部件 —— 五大件逐行高亮
        self.beat([LaggedStart(
            lines[1].animate.set_color(GOLD),
            lines[2].animate.set_color(GOLD),
            lines[3].animate.set_color(GOLD),
            lines[4].animate.set_color(GOLD),
            lines[5].animate.set_color(GOLD),
            lag_ratio=0.18)], intro_t=0.8)
        # 14 待会瞄一眼它开头几行
        self.beat([Indicate(lines[1], color=ACCENT), Indicate(lines[2], color=ACCENT)], intro_t=0.5)
        # 15 这集不深讲代码先认脸
        self.beat([Circumscribe(card, color=ACCENT)], intro_t=0.6)

        self.P(FadeOut(title, left, lcap, card, arrow), run_time=0.5)

    # ------------------------------------------------------------------
    # 段2 主角登场 — 16 句
    # ------------------------------------------------------------------
    def seg2_cast(self):
        title = self.head("五大件 + 一位隐形指挥")
        cpu = self.node("CPU\nARM7 处理器", ACCENT, w=2.5, h=1.1, fs=17)
        mem = self.node("内存\nMemory", BLUE, w=2.5, h=1.1, fs=17)
        ppu = self.node("PPU\n画面处理", GOLD, w=2.5, h=1.1, fs=17)
        apu = self.node("APU\n声音处理", WARN, w=2.5, h=1.1, fs=17)
        dma = self.node("DMA\n高速搬运", "#c77dff", w=2.5, h=1.1, fs=17)
        top = VGroup(cpu, mem, ppu).arrange(RIGHT, buff=0.5)
        bot = VGroup(apu, dma).arrange(RIGHT, buff=0.5)
        cards = VGroup(top, bot).arrange(DOWN, buff=0.5).move_to(UP * 0.3)
        clock_line = Line(LEFT * 5.5, RIGHT * 5.5, color=GOLD, stroke_width=3).to_edge(DOWN, buff=0.9)
        clock_lbl = Text("时钟 · 一根贯穿全场的节拍", font=CN, font_size=18, color=GOLD).next_to(clock_line, UP, buff=0.12)

        # 1 请出主角们
        self.beat([Write(title)], intro_t=0.6)
        # 2 五大件加一位隐形指挥
        self.beat([Indicate(title, color=GOLD, scale_factor=1.05)], intro_t=0.5)
        # 3 第一位CPU处理器
        self.beat([FadeIn(cpu, scale=0.85)], intro_t=0.5)
        # 4 GBA用ARM7处理器
        self.beat([Indicate(cpu, color=ACCENT)], intro_t=0.5)
        # 5 执行每条指令是大脑
        self.beat([Circumscribe(cpu, color=ACCENT)], intro_t=0.5)
        # 6 第二位内存
        self.beat([FadeIn(mem, scale=0.85)], intro_t=0.5)
        # 7 代码数据画面都存这
        self.beat([Indicate(mem, color=BLUE)], intro_t=0.5)
        # 8 像巨大草稿纸谁都能读写
        self.beat([Circumscribe(mem, color=BLUE)], intro_t=0.5)
        # 9 第三位PPU画面处理单元
        self.beat([FadeIn(ppu, scale=0.85)], intro_t=0.5)
        # 10 把内存数据画成像素
        self.beat([Indicate(ppu, color=GOLD)], intro_t=0.5)
        # 11 屏幕上一切出自它手
        self.beat([Circumscribe(ppu, color=GOLD)], intro_t=0.5)
        # 12 第四位APU声音处理单元合成音效音乐
        self.beat([FadeIn(apu, scale=0.85), Indicate(apu, color=WARN)], intro_t=0.6)
        # 13 第五位DMA高速搬运工
        self.beat([FadeIn(dma, scale=0.85)], intro_t=0.5)
        # 14 搬大量数据且不打扰CPU
        self.beat([Indicate(dma, color="#c77dff"), Indicate(cpu, color=GREY)], intro_t=0.6)
        # 15 最后那位隐形指挥
        self.beat([Create(clock_line), FadeIn(clock_lbl)], intro_t=0.6)
        # 16 一根贯穿全场的时钟让所有部件对上拍子
        self.beat([Indicate(clock_line, color=GOLD), Indicate(clock_lbl, color=GOLD)], intro_t=0.6)

        self.P(FadeOut(title, cards, clock_line, clock_lbl), run_time=0.5)

    # ------------------------------------------------------------------
    # 段3 一帧的旅程·上 — 14 句
    # ------------------------------------------------------------------
    def seg3_journey_a(self):
        title = self.head("一帧的旅程 · 上")
        nodes, arrows = self.flow_pipeline(
            ["手指 / 按键", "内存", "CPU", "显存"],
            [GREY, BLUE, ACCENT, GOLD], y=0.2, w=2.6, h=1.0, gap=0.6)

        # 1 旅程开始
        self.beat([Write(title), FadeIn(nodes[0], scale=0.85)], intro_t=0.6)
        # 2 起点是手指
        self.beat([Indicate(nodes[0], color=GREY)], intro_t=0.5)
        # 3 按A键记录到内存固定位置
        self.beat([FadeIn(nodes[1], scale=0.85), GrowArrow(arrows[0])], intro_t=0.6)
        # 4 机器知道玩家按了键
        self.beat([Indicate(nodes[1], color=BLUE)], intro_t=0.5)
        # 5 接力棒交给CPU
        self.beat([FadeIn(nodes[2], scale=0.85), GrowArrow(arrows[1])], intro_t=0.6)
        # 6 CPU干活是重复一个循环
        self.beat([Circumscribe(nodes[2], color=ACCENT)], intro_t=0.5)
        # 7 第一步取指从内存取出指令
        self.beat([Indicate(nodes[2], color=GOLD, scale_factor=1.25)], intro_t=0.5)
        # 8 第二步解码搞清要干什么
        self.beat([Indicate(nodes[2], color=BLUE, scale_factor=1.25)], intro_t=0.5)
        # 9 第三步执行真正做掉
        self.beat([Indicate(nodes[2], color=ACCENT, scale_factor=1.25)], intro_t=0.5)
        # 10 取指解码执行周而复始
        self.beat([Circumscribe(nodes[2], color=GOLD)], intro_t=0.5)
        # 11 几百万条指令流过CPU
        self.beat([Indicate(nodes[2], color=ACCENT, scale_factor=1.3)], intro_t=0.5)
        # 12 算出马里奥这帧站哪
        self.beat([Indicate(nodes[2], color=GOLD)], intro_t=0.5)
        # 13 算完结果写回内存
        self.beat([Indicate(nodes[1], color=BLUE, scale_factor=1.2)], intro_t=0.5)
        # 14 把画面数据写进显存
        self.beat([FadeIn(nodes[3], scale=0.85), GrowArrow(arrows[2]), Indicate(nodes[3], color=GOLD)], intro_t=0.6)

        self.P(FadeOut(title, nodes, arrows), run_time=0.5)

    # ------------------------------------------------------------------
    # 段4 一帧的旅程·下 — 14 句
    # ------------------------------------------------------------------
    def seg4_journey_b(self):
        title = self.head("一帧的旅程 · 下")
        vram = self.node("显存\nVRAM", GOLD, w=2.2, h=0.9, fs=16)
        dma = self.node("DMA\n搬运", "#c77dff", w=2.2, h=0.9, fs=16)
        ppu = self.node("PPU\n绘制", ACCENT, w=2.2, h=0.9, fs=16)
        chain = VGroup(vram, dma, ppu).arrange(RIGHT, buff=0.7).to_edge(LEFT, buff=0.7).shift(UP * 1.6)
        a1 = Arrow(vram.get_right(), dma.get_left(), color=GREY, buff=0.08, stroke_width=3)
        a2 = Arrow(dma.get_right(), ppu.get_left(), color=GREY, buff=0.08, stroke_width=3)

        screen = RoundedRectangle(width=4.6, height=3.0, corner_radius=0.15,
                                  color=BLUE, stroke_width=3,
                                  fill_color="#10243a", fill_opacity=0.85).shift(DOWN * 1.0 + RIGHT * 1.5)
        scanline = Line(screen.get_left(), screen.get_right(), color=ACCENT, stroke_width=4)
        scanline.move_to(np.array([screen.get_center()[0], screen.get_top()[1] - 0.12, 0]))

        # 1 数据进显存还只是数字
        self.beat([Write(title), FadeIn(vram, scale=0.85)], intro_t=0.6)
        # 2 要变画面得有人搬有人画
        self.beat([Indicate(vram, color=GOLD)], intro_t=0.5)
        # 3 DMA登场
        self.beat([FadeIn(dma, scale=0.85), GrowArrow(a1)], intro_t=0.6)
        # 4 高速把数据搬进画面缓冲区
        self.beat([FadeIn(screen), GrowArrow(a2), FadeIn(ppu, scale=0.85)], intro_t=0.6)
        # 5 不打扰CPU可继续算下一帧
        self.beat([Indicate(dma, color="#c77dff")], intro_t=0.5)
        # 6 轮到PPU出场
        self.beat([Circumscribe(ppu, color=ACCENT)], intro_t=0.5)
        # 7 PPU画画很有意思一行一行画
        self.beat([Create(scanline)], intro_t=0.5)
        # 8 屏幕从上到下切成很多横线
        grid = VGroup(*[Line(screen.get_left(), screen.get_right(), color=GREY, stroke_width=0.6)
                        .move_to(np.array([screen.get_center()[0], y, 0]))
                        for y in np.linspace(screen.get_bottom()[1] + 0.2, screen.get_top()[1] - 0.2, 10)])
        grid.set_z_index(-1)
        self.beat([FadeIn(grid)], intro_t=0.5)
        # 9 每条叫扫描线
        self.beat([Indicate(scanline, color=ACCENT, scale_factor=1.05)], intro_t=0.5)
        # 10 PPU从最上面那行画起
        self.beat([scanline.animate.move_to(np.array([screen.get_center()[0], screen.get_top()[1] - 0.3, 0]))], intro_t=0.5)
        # 11 画完一行往下挪再画
        self.beat([scanline.animate.move_to(screen.get_center())], intro_t=0.6)
        # 12 一直画到最底下那行
        self.beat([scanline.animate.move_to(np.array([screen.get_center()[0], screen.get_bottom()[1] + 0.2, 0]))], intro_t=0.6)
        # 13 最后一行画完整帧亮起
        self.beat([Flash(screen, color=GOLD, line_length=0.3, num_lines=24),
                   screen.animate.set_fill(ACCENT, opacity=0.25)], intro_t=0.7)
        # 14 同时APU合成好这帧声音
        note = Text("♪", font_size=44, color=WARN).next_to(screen, UP, buff=0.1)
        self.beat([FadeIn(note, shift=UP), Indicate(note, color=WARN, scale_factor=1.4)], intro_t=0.6)

        self.P(FadeOut(title, chain, a1, a2, screen, scanline, grid, note), run_time=0.5)

    # ------------------------------------------------------------------
    # 段5 隐形的主宰 — 13 句
    # ------------------------------------------------------------------
    def seg5_clock(self):
        title = self.head("隐形的主宰 · 时钟")
        mods = VGroup(
            self.node("CPU", ACCENT, w=1.8, h=0.7, fs=16),
            self.node("DMA", "#c77dff", w=1.8, h=0.7, fs=16),
            self.node("PPU", GOLD, w=1.8, h=0.7, fs=16),
            self.node("APU", WARN, w=1.8, h=0.7, fs=16),
        ).arrange(RIGHT, buff=0.5).to_edge(UP, buff=1.4)

        axis = Line(LEFT * 5.5, RIGHT * 5.5, color=GOLD, stroke_width=3).shift(UP * 0.1)
        ticks = VGroup()
        for x in np.linspace(-5.0, 5.0, 11):
            ticks.add(Line(UP * 0.12, DOWN * 0.12, color=GOLD, stroke_width=2)
                      .move_to(np.array([x, axis.get_center()[1], 0])))
        axlbl = Text("时钟周期 cycle", font=CN, font_size=16, color=GOLD).next_to(axis, DOWN, buff=0.15).to_edge(LEFT, buff=1.0)

        card = self.code_card(
            ["void mTimingSchedule(",
             "　　struct mTiming*,",
             "　　struct mTimingEvent*,",
             "　　int32_t when);"],
            title="src/core/timing.c", w=6.5, fs=19)
        card.to_edge(DOWN, buff=0.6)

        # 1 你可能冒出一个问题
        self.beat([Write(title)], intro_t=0.6)
        # 2 这么多部件凭什么对得齐
        self.beat([LaggedStart(*[FadeIn(m, scale=0.85) for m in mods], lag_ratio=0.2)], intro_t=0.7)
        # 3 答案是那位隐形指挥
        self.beat([Indicate(title, color=GOLD, scale_factor=1.05)], intro_t=0.5)
        # 4 那根贯穿全场的时钟
        self.beat([Create(axis), LaggedStart(*[Create(t) for t in ticks], lag_ratio=0.05), FadeIn(axlbl)], intro_t=0.7)
        # 5 每个部件每件事都花确定数量时钟周期
        self.beat([LaggedStart(*[Indicate(m, color=GOLD) for m in mods], lag_ratio=0.15)], intro_t=0.6)
        # 6 取指搬数据画像素各有耗时
        self.beat([LaggedStart(*[Flash(t, color=ACCENT, line_length=0.15) for t in ticks[:6]], lag_ratio=0.06)], intro_t=0.6)
        # 7 时钟一拍拍走谁也快不了慢不了
        self.beat([LaggedStart(*[Flash(t, color=GOLD, line_length=0.15) for t in ticks], lag_ratio=0.05)], intro_t=0.7)
        # 8 mGBA怎么管住这一切
        self.beat([FadeIn(card[0]), FadeIn(card[2] if len(card) > 2 else card[1])], intro_t=0.6)
        # 9 用一个事件调度器统一管理时间
        self.beat([Write(card[1])], intro_t=0.7)
        # 10 这套调度藏在源码timing模块里
        self.beat([Indicate(card[2] if len(card) > 2 else card[0], color=ACCENT)], intro_t=0.5)
        # 11 谁该在第几周期做啥安排得明明白白
        self.beat([Circumscribe(card, color=GOLD)], intro_t=0.6)
        # 12 正是这套安排让部件严丝合缝
        self.beat([LaggedStart(*[Indicate(m, color=ACCENT) for m in mods], lag_ratio=0.15)], intro_t=0.6)
        # 13 这恰是写模拟器最核心难题周期精确
        emph = Text("周期精确", font=CN, font_size=26, color=ACCENT, weight=BOLD).next_to(mods, DOWN, buff=0.35)
        self.beat([Write(emph), Circumscribe(emph, color=GOLD)], intro_t=0.7)

        self.P(FadeOut(title, mods, axis, ticks, axlbl, card, emph), run_time=0.5)

    # ------------------------------------------------------------------
    # 段6 系列地图 — 8 句
    # ------------------------------------------------------------------
    def seg6_map(self):
        title = self.head("系列地图 · 这趟旅程")
        labels = ["序章", "CPU", "指令", "内存", "时钟", "DMA", "PPU", "BIOS", "声音", "存档"]
        stations = VGroup()
        for lbl in labels:
            d = Dot(radius=0.13, color=GREY)
            t = Text(lbl, font=CN, font_size=15, color="#d7e3fc").next_to(d, DOWN, buff=0.12)
            stations.add(VGroup(d, t))
        # 排成两行折线地图
        row1 = VGroup(*stations[:5]).arrange(RIGHT, buff=1.05)
        row2 = VGroup(*stations[5:]).arrange(LEFT, buff=1.05)
        VGroup(row1, row2).arrange(DOWN, buff=1.3).move_to(ORIGIN)

        # 连线
        path = VGroup()
        for i in range(len(stations) - 1):
            path.add(Line(stations[i][0].get_center(), stations[i + 1][0].get_center(),
                          color=GREY, stroke_width=2))
        path.set_z_index(-1)

        # 1 把旅程画成一张地图
        self.beat([Write(title)], intro_t=0.6)
        # 2 按键CPU内存DMA PPU APU还有时钟（站点逐个出现）
        self.beat([LaggedStart(*[FadeIn(s, scale=0.7) for s in stations], lag_ratio=0.12)], intro_t=0.8)
        # 3 这地图是整个系列骨架
        self.beat([Create(path)], intro_t=0.7)
        # 4 往后每集停在某一站深挖
        self.beat([LaggedStart(*[Indicate(s[0], color=GOLD) for s in stations], lag_ratio=0.1)], intro_t=0.7)
        # 5 下一集从CPU开始
        self.beat([stations[1][0].animate.set_color(ACCENT).scale(1.4), Indicate(stations[1], color=ACCENT)], intro_t=0.6)
        # 6 聊软件怎么假装成芯片
        self.beat([Circumscribe(stations[1], color=ACCENT)], intro_t=0.5)
        # 7 今天只看了轮廓
        self.beat([stations[0][0].animate.set_color(ACCENT).scale(1.4), Indicate(stations[0], color=ACCENT)], intro_t=0.6)
        # 8 这趟旅程才刚刚开始
        ending = Text("这趟旅程，才刚刚开始", font=CN, font_size=30, color=GOLD, weight=BOLD).to_edge(DOWN, buff=0.7)
        self.beat([Write(ending)], intro_t=0.9)

        self.P(FadeOut(title, stations, path, ending), run_time=0.5)
