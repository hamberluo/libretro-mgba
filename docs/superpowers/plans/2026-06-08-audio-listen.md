# 全站「听书」功能实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给文档站 10 篇正文各加一个顶部音频听书播放器：edge-tts 预生成 mp3（散文照念、代码块/组件转提示）、AudioPlayer 组件（播放/进度/倍速）、10 篇接入、部署。

**Architecture:** 本地 Python 脚本读 guide/*.md 清洗成朗读文本并用 edge-tts 生成 mp3 到 public/audio/（进 git）；新增 AudioPlayer.vue 全局组件（withBase 处理 base 前缀）；10 篇 md 顶部各插一行组件标签。复用已验证产线：写文件 → 生成 mp3 → build 验证 → 抽听+截图 → 合并 master → push 部署。

**Tech Stack:** Python3 + edge-tts、Vue 3、VitePress 1.6.4（base `/libretro-mgba/`）、已有 GitHub Actions 部署。

**关键事实（已核实）：**
- edge-tts 本机可用；实测 ~352 KB/分钟，10 篇合计约 12-18 MB。
- 10 篇 slug：intro, ep02-cpu, ep03-thumb, ep04-memory, ep05-timing, ep06-dma, ep07-ppu, ep08-bios, ep09-audio, ep10-savestate。
- VitePress base `/libretro-mgba/`；public/ 拷到站点根；组件内 src 用 `withBase` 加前缀。
- 组件注册在 `website/.vitepress/theme/index.ts`（已 10 个，本功能加第 11 个 AudioPlayer）。
- 部署触发：push master 改 `website/**`。
- **统一规范**：按钮/控件 hover `rgba(0,212,170,0.12)`、主色 `#00d4aa`、数字 nowrap、暗色。

**「测试」语义：** 前端 + 音频。判据：脚本跑出 10 个 mp3；抽听念的是散文（代码块/组件转提示语）；vitepress build 无死链；headless 截图播放器渲染正常；线上能播。

---

## File Structure

| 文件 | 责任 |
|------|------|
| `website/tools/build-listen-audio.py` | 读 guide/*.md → 清洗朗读文本 → edge-tts 生成 mp3 |
| `website/public/audio/<slug>.mp3` ×10 | 各篇听书音频（产物，进 git） |
| `website/components/AudioPlayer.vue` | 播放器组件（播放/进度/时长/倍速） |
| `website/.vitepress/theme/index.ts` | 增注册 AudioPlayer |
| `website/guide/*.md` ×10 | 各篇顶部插 `<AudioPlayer src="audio/<slug>.mp3" />` |

---

## Task 1: 音频生成脚本 + 跑出 10 个 mp3

**Files:**
- Create: `website/tools/build-listen-audio.py`
- Create (产物): `website/public/audio/*.mp3`

- [ ] **Step 1: 写 build-listen-audio.py**

创建 `website/tools/build-listen-audio.py`：

```python
#!/usr/bin/env python3
"""读 website/guide/*.md，清洗成朗读文本，用 edge-tts 生成 mp3 到 website/public/audio/。
用法：
  python3 tools/build-listen-audio.py            # 生成全部 10 篇
  python3 tools/build-listen-audio.py ep05-timing # 只生成指定篇
"""
import asyncio, re, sys
from pathlib import Path
import edge_tts

WEB = Path(__file__).resolve().parent.parent      # website/
GUIDE = WEB / "guide"
OUT = WEB / "public" / "audio"
OUT.mkdir(parents=True, exist_ok=True)

VOICE = "zh-CN-XiaoxiaoNeural"
RATE = "+30%"

SLUGS = ["intro", "ep02-cpu", "ep03-thumb", "ep04-memory", "ep05-timing",
         "ep06-dma", "ep07-ppu", "ep08-bios", "ep09-audio", "ep10-savestate"]


def clean(md: str) -> str:
    """markdown -> 朗读纯文本（散文照念；代码/组件转提示；链接/表格去掉）。"""
    # 1) 代码块 ```...``` 整段 -> 提示语
    md = re.sub(r"```.*?```", "\n这里有一段源码，详见网页。\n", md, flags=re.DOTALL)
    out = []
    for line in md.splitlines():
        s = line.rstrip()
        if not s.strip():
            out.append("")
            continue
        # 2) 组件标签 <XxxDemo /> -> 提示语
        if re.match(r"^\s*<[A-Z][A-Za-z]+\s*/?>", s):
            out.append("这里有一个交互演示，可以在网页上动手试试。")
            continue
        # 3) 源码引用块 > ↗ ... -> 删除
        if "↗" in s and s.lstrip().startswith(">"):
            continue
        # 4) markdown 表格行 -> 删除
        if re.match(r"^\s*\|.*\|\s*$", s):
            continue
        # 5) 标题 -> 去井号
        s = re.sub(r"^#{1,6}\s*", "", s)
        # 6) 列表标记 -> 去掉
        s = re.sub(r"^\s*[-*]\s+", "", s)
        # 7) 行内 markdown：粗体/行内码/链接
        s = re.sub(r"\*\*([^*]+)\*\*", r"\1", s)
        s = re.sub(r"`([^`]+)`", r"\1", s)
        s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
        out.append(s)
    text = "\n".join(out)
    # 8) 多空行压成单空行（段落停顿）
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


async def synth(slug: str):
    md = (GUIDE / f"{slug}.md").read_text(encoding="utf-8")
    text = clean(md)
    if not text:
        print(f"[{slug}] 清洗后为空，跳过"); return
    comm = edge_tts.Communicate(text, VOICE, rate=RATE)
    await comm.save(str(OUT / f"{slug}.mp3"))
    print(f"[{slug}] 生成 {(OUT / f'{slug}.mp3').stat().st_size // 1024} KB")


async def main(slugs):
    for slug in slugs:
        await synth(slug)


if __name__ == "__main__":
    targets = sys.argv[1:] or SLUGS
    asyncio.run(main(targets))
```

- [ ] **Step 2: 干跑清洗逻辑（不生成音频，先看朗读文本对不对）**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
python3 -c "
import sys; sys.path.insert(0, 'tools')
import importlib.util
spec = importlib.util.spec_from_file_location('b', 'tools/build-listen-audio.py')
m = importlib.util.module_from_spec(spec)
# 只测 clean()，不触发 edge_tts 网络
import re
src = open('tools/build-listen-audio.py',encoding='utf-8').read()
exec(re.search(r'def clean.*?return text.strip\(\)', src, re.DOTALL).group(0).replace('def clean(md: str) -> str:','def clean(md):'))
t = clean(open('guide/ep05-timing.md',encoding='utf-8').read())
print(t[:600])
print('---- 含提示语检查 ----')
print('源码提示:', '这里有一段源码' in t)
print('组件提示:', '这里有一个交互演示' in t)
print('无残留代码块:', '\`\`\`' not in t)
print('无残留↗链接:', '↗' not in t)
"
```
Expected: 打印清洗后的朗读文本（应是通顺中文散文，代码块处是「这里有一段源码」、组件处是「这里有一个交互演示」）；四项检查：源码提示 True、组件提示 True、无残留代码块 True、无残留↗链接 True。若清洗有问题（残留 markdown 标记/代码），调 clean() 规则。

- [ ] **Step 3: 生成全部 10 个 mp3**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
python3 tools/build-listen-audio.py
ls -la public/audio/
du -sh public/audio/
```
Expected: 打印 10 行「[slug] 生成 XXX KB」；public/audio/ 下有 10 个 mp3；总大小约 12-20 MB。

- [ ] **Step 4: 抽听验证（人工/抽查时长）**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
for f in public/audio/intro.mp3 public/audio/ep05-timing.mp3; do
  echo -n "$f: "; ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$f"
done
# 如需试听：afplay public/audio/ep05-timing.mp3
```
Expected: 每篇时长在 2-7 分钟（120-420 秒）区间。若某篇异常短（<60s）说明清洗把正文删多了，回 Step1 调规则重跑该篇。

- [ ] **Step 5: 提交脚本 + 音频**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/tools/build-listen-audio.py website/public/audio/
git status   # 确认 10 个 mp3 + 脚本入暂存（public/audio 不被 gitignore）
git commit -m "feat(docsite): 听书音频生成脚本 + 10 篇 mp3

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```
Expected: 提交含 1 脚本 + 10 mp3。

---

## Task 2: AudioPlayer 播放器组件

**Files:**
- Create: `website/components/AudioPlayer.vue`
- Modify: `website/.vitepress/theme/index.ts`

- [ ] **Step 1: 写 AudioPlayer.vue**

创建 `website/components/AudioPlayer.vue`：

```vue
<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { withBase } from 'vitepress'

const props = defineProps({ src: { type: String, required: true } })

const audio = ref(null)
const playing = ref(false)
const cur = ref(0)
const dur = ref(0)
const rates = [1, 1.25, 1.5, 0.75]
const rateIdx = ref(0)
const rate = computed(() => rates[rateIdx.value])

const resolvedSrc = computed(() => withBase(props.src))

function fmt(t) {
  if (!t || isNaN(t)) return '0:00'
  const m = Math.floor(t / 60)
  const s = Math.floor(t % 60)
  return m + ':' + String(s).padStart(2, '0')
}
const progress = computed(() => dur.value ? (cur.value / dur.value) * 100 : 0)

function toggle() {
  const a = audio.value
  if (!a) return
  if (a.paused) { a.play(); playing.value = true }
  else { a.pause(); playing.value = false }
}
function onTime() { cur.value = audio.value.currentTime }
function onMeta() { dur.value = audio.value.duration }
function onEnd() { playing.value = false }
function seek(e) {
  const a = audio.value
  if (!a || !dur.value) return
  const rect = e.currentTarget.getBoundingClientRect()
  const ratio = (e.clientX - rect.left) / rect.width
  a.currentTime = ratio * dur.value
}
function cycleRate() {
  rateIdx.value = (rateIdx.value + 1) % rates.length
  if (audio.value) audio.value.playbackRate = rate.value
}

onMounted(() => { if (audio.value) audio.value.playbackRate = rate.value })
onUnmounted(() => { if (audio.value) audio.value.pause() })
</script>

<template>
  <div class="ap">
    <audio
      ref="audio"
      :src="resolvedSrc"
      preload="metadata"
      @timeupdate="onTime"
      @loadedmetadata="onMeta"
      @ended="onEnd"
    ></audio>
    <button class="play" @click="toggle" :aria-label="playing ? '暂停' : '播放'">
      <span v-if="!playing">▶</span><span v-else>❚❚</span>
    </button>
    <span class="label">听书</span>
    <div class="bar" @click="seek">
      <div class="fill" :style="{ width: progress + '%' }"></div>
    </div>
    <span class="time">{{ fmt(cur) }} / {{ fmt(dur) }}</span>
    <button class="rate" @click="cycleRate">{{ rate }}×</button>
  </div>
</template>

<style scoped>
.ap {
  display: flex; align-items: center; gap: 0.7rem;
  padding: 0.6rem 0.9rem; margin: 1rem 0 1.5rem;
  border: 1px solid var(--vp-c-divider); border-radius: 10px;
  background: var(--vp-c-bg-soft);
}
.play, .rate {
  flex-shrink: 0; border: 1px solid #00d4aa; background: transparent; color: #00d4aa;
  border-radius: 8px; cursor: pointer; transition: background 0.2s;
}
.play { width: 2rem; height: 2rem; font-size: 0.8rem; display: flex; align-items: center; justify-content: center; }
.rate { padding: 0.25rem 0.6rem; font-size: 0.8rem; font-family: var(--vp-font-family-mono); white-space: nowrap; }
.play:hover, .rate:hover { background: rgba(0, 212, 170, 0.12); }
.label { flex-shrink: 0; font-size: 0.85rem; color: var(--vp-c-text-2); white-space: nowrap; }
.bar { flex: 1; min-width: 60px; height: 6px; background: var(--vp-c-bg); border-radius: 3px; cursor: pointer; overflow: hidden; }
.fill { height: 100%; background: #00d4aa; border-radius: 3px; transition: width 0.1s linear; }
.time { flex-shrink: 0; font-size: 0.78rem; color: var(--vp-c-text-3); font-family: var(--vp-font-family-mono); white-space: nowrap; }
@media (max-width: 640px) {
  .label { display: none; }
}
</style>
```

- [ ] **Step 2: theme/index.ts 增注册 AudioPlayer（保留已有 10 个组件）**

把 `website/.vitepress/theme/index.ts` 的 import 区加一行、enhanceApp 加一行注册。在现有 10 个 import 后加：
```ts
import AudioPlayer from '../../components/AudioPlayer.vue'
```
在现有 10 个 `app.component(...)` 后加：
```ts
    app.component('AudioPlayer', AudioPlayer)
```
（其余保持不变。）

- [ ] **Step 3: 临时页验证组件 SSR 渲染**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
printf '# 临时\n<AudioPlayer src="audio/intro.mp3" />\n' > guide/_tmp_ap.md
npx vitepress build 2>&1 | tail -5
python3 -c "
h=open('.vitepress/dist/guide/_tmp_ap.html',encoding='utf-8').read()
for k in ['听书','class=\"ap\"','audio/intro.mp3']:
    print(k, k in h)
"
rm -f guide/_tmp_ap.md
rm -rf .vitepress/dist .vitepress/cache
```
Expected: build 成功；python 校验：听书 True、`class="ap"` True、`audio/intro.mp3`（经 withBase 应渲染为含 /libretro-mgba/ 的路径，故直接搜 `audio/intro.mp3` 子串应 True）。临时页已删。

- [ ] **Step 4: 提交组件**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/components/AudioPlayer.vue website/.vitepress/theme/index.ts
git status   # 无临时页、无 dist/cache
git commit -m "feat(docsite): AudioPlayer 听书播放器组件（播放/进度/倍速）

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: 10 篇正文接入播放器

**Files:**
- Modify: `website/guide/*.md` ×10

- [ ] **Step 1: 每篇一级标题后插入播放器标签**

对每个 guide 文件，在其一级标题（`# …` 那行）的下一行插入一行播放器标签（src 用该篇 slug）。逐篇操作：

| 文件 | 在标题后插入 |
|------|------|
| `guide/intro.md` | `<AudioPlayer src="audio/intro.mp3" />` |
| `guide/ep02-cpu.md` | `<AudioPlayer src="audio/ep02-cpu.mp3" />` |
| `guide/ep03-thumb.md` | `<AudioPlayer src="audio/ep03-thumb.mp3" />` |
| `guide/ep04-memory.md` | `<AudioPlayer src="audio/ep04-memory.mp3" />` |
| `guide/ep05-timing.md` | `<AudioPlayer src="audio/ep05-timing.mp3" />` |
| `guide/ep06-dma.md` | `<AudioPlayer src="audio/ep06-dma.mp3" />` |
| `guide/ep07-ppu.md` | `<AudioPlayer src="audio/ep07-ppu.mp3" />` |
| `guide/ep08-bios.md` | `<AudioPlayer src="audio/ep08-bios.mp3" />` |
| `guide/ep09-audio.md` | `<AudioPlayer src="audio/ep09-audio.mp3" />` |
| `guide/ep10-savestate.md` | `<AudioPlayer src="audio/ep10-savestate.mp3" />` |

具体做法：对每篇用 Edit，把第一行的标题替换为「标题 + 换行 + 空行 + 播放器标签」。例如 ep05：
old:
```
# 时间的主宰 · 周期精确与事件调度
```
new:
```
# 时间的主宰 · 周期精确与事件调度

<AudioPlayer src="audio/ep05-timing.mp3" />
```
其余 9 篇同理（标题文字各不同，按各篇实际一级标题改；src 用对应 slug）。

- [ ] **Step 2: 构建验证 + 抽查接入**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
npx vitepress build 2>&1 | tail -8
python3 -c "
import glob
miss=[]
for f in glob.glob('guide/*.md'):
    h=open(f,encoding='utf-8').read()
    if '<AudioPlayer' not in h: miss.append(f)
print('缺播放器的篇:', miss if miss else '无，10 篇全接入')
"
rm -rf .vitepress/dist .vitepress/cache
```
Expected: build 成功无死链；「缺播放器的篇: 无，10 篇全接入」。

- [ ] **Step 3: 提交接入**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/guide/
git commit -m "feat(docsite): 10 篇正文顶部接入听书播放器

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: 部署上线与验证

**Files:** 无（合并 + push）

- [ ] **Step 1: 合并 feature 分支回 master**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git checkout master
git merge --no-ff feat/audio-listen -m "Merge: 全站听书功能上线

每篇顶部音频播放器 + edge-tts 生成的 10 篇 mp3 + AudioPlayer 组件。

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git branch -d feat/audio-listen
```

- [ ] **Step 2: push 触发部署**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git push origin master
```
Expected: push 成功（含 10 个 mp3，体积稍大，push 慢一点正常）。

- [ ] **Step 3: 跟踪 Actions 到成功**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
gh run list --workflow=docs.yml --limit 2
```
Expected: 最新 docs workflow completed/success。

- [ ] **Step 4: 线上验证**

```bash
# 页面有播放器
curl -s https://hamberluo.github.io/libretro-mgba/guide/ep05-timing.html | python3 -c "import sys;h=sys.stdin.read();print('播放器:', 'class=\"ap\"' in h or '听书' in h)"
# 音频文件可访问
curl -s -o /dev/null -w "intro.mp3: %{http_code} %{size_download}bytes\n" https://hamberluo.github.io/libretro-mgba/audio/intro.mp3
curl -s -o /dev/null -w "ep05.mp3: %{http_code}\n" https://hamberluo.github.io/libretro-mgba/audio/ep05-timing.mp3
```
Expected: 播放器: True；intro.mp3 返回 200 且 size 为数 MB；ep05 也 200。（控制器随后 headless 截图自查播放器渲染 + 实际点播一篇确认能出声。）
