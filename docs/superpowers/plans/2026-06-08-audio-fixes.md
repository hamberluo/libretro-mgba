# 听书功能两项改进实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** ① 修复 TTS 多音字「行」(háng 读成 xíng)——清洗脚本加 háng 语境替换、重生成受影响 mp3；② AudioPlayer 加滚动悬浮 mini 播放器,滚出顶部后右下角常驻可暂停。

**Architecture:** 改 `build-listen-audio.py` 的 clean() 在喂 TTS 前把 háng 语境的「行」替换为同音字「航」(正文 md 不动、只影响朗读)；重跑受影响篇的 mp3。AudioPlayer 组件内用 IntersectionObserver 监测顶部条是否离开视口,播放中且离开时渲染 `position:fixed` mini 条,复用同一 audio 状态。

**Tech Stack:** Python3+edge-tts、Vue 3、VitePress。

**关键事实（已核实）：**
- 受「行」读音影响的篇（dry-run 命中 háng 搭配）：intro, ep02-cpu, ep03-thumb, ep04-memory, ep05-timing, ep06-dma, ep07-ppu, ep09-audio, ep10-savestate（9 篇；ep08-bios 无 háng 搭配，不重生成）。
- 替换只针对 háng 固定搭配，不碰 执行/运行/行为/并行（读 xíng 正确）。
- AudioPlayer 在 components/AudioPlayer.vue，已用 useData 取 base（上个修复）。
- 部署：push master 改 website/** 触发。

**「测试」语义：** 脚本跑出新 mp3（抽查时长不变、文件更新）；前端 build 无死链 + headless 验证滚动后出现悬浮条、点暂停生效。

---

## Task 1: 清洗脚本加 háng 替换 + 重生成 9 篇 mp3

**Files:**
- Modify: `website/tools/build-listen-audio.py`
- Modify (产物): 受影响 9 篇 mp3

- [ ] **Step 1: 给 clean() 末尾加 háng 替换**

在 `website/tools/build-listen-audio.py` 的 `clean()` 函数里，`text = re.sub(r"\n{3,}", "\n\n", text)` 之后、`return text.strip()` 之前，插入 háng 替换逻辑：

```python
    # 多音字修正：把 háng（排、行列）语境的「行」替换为同音字「航」，让 TTS 读对。
    # 不碰 执行/运行/行为/并行/进行（读 xíng）——这些搭配不在替换列表里。
    hang_phrases = [
        "一行行", "一行一行", "画一行", "下一行", "上一行", "这一行", "那一行",
        "最后一行", "每一行", "第一行", "再画下一行", "一行往下", "行往下挪",
        "一行画完", "一行的像素", "一行代码", "第几行",
    ]
    for ph in hang_phrases:
        text = text.replace(ph, ph.replace("行", "航"))
    # 兜底：剩余「N 行」「几行」这类计数（紧跟"行"前是数字或"几/多少"且语义为行数）
    text = re.sub(r"(\d+)\s*行", r"\1 航", text)
    text = text.replace("几行", "几航")
```

注意：替换顺序——长搭配在前（已按长度大致排序），`replace` 幂等，重叠搭配（如「画下一行」含「下一行」）先被长的或先匹配的处理，"行"→"航"后短搭配里已无"行"，无重复替换问题。

- [ ] **Step 2: 干跑验证替换正确（háng 改对、xíng 没误伤）**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
python3 -c "
import importlib.util
spec=importlib.util.spec_from_file_location('b','tools/build-listen-audio.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
t=m.clean(open('guide/intro.md',encoding='utf-8').read())
# 应有：航（被替换的háng）；应保留：执行/运行/行为（xíng）
import re
print('含「一航一航」或「画一航」:', '一航' in t or '画一航' in t)
print('保留「执行」:', '执行' in t)
print('保留「运行」(若原文有):', '运行' in t or True)
print('误伤检查—不该出现「执航/运航/航为」:', not any(x in t for x in ['执航','运航','航为','并航']))
# 打印含航/行的片段抽查
for seg in re.findall(r'.{0,4}[行航].{0,4}', t)[:12]:
    print('  ', seg)
"
```
Expected: 「一航/画一航」True、「执行」保留 True、误伤检查 True（无 执航/运航/航为/并航）。抽查片段里:扫描线相关的是「航」、执行/运行相关的是「行」。若有误伤，调 hang_phrases。

- [ ] **Step 3: 重生成受影响的 9 篇 mp3**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
python3 tools/build-listen-audio.py intro ep02-cpu ep03-thumb ep04-memory ep05-timing ep06-dma ep07-ppu ep09-audio ep10-savestate
ls -la public/audio/ | grep -E "intro|ep0[234579]|ep10"
```
Expected: 9 行「[slug] 生成 XXX KB」；对应 mp3 文件 mtime 更新。（ep08-bios 不重生成，无 háng 搭配。）

- [ ] **Step 4: 抽查时长仍正常**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
for f in intro ep07-ppu; do
  echo -n "$f: "; ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "public/audio/$f.mp3"
done
```
Expected: 时长仍在 120-420s 区间（替换同音字不改变时长量级）。

- [ ] **Step 5: 提交脚本 + 更新的 mp3**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/tools/build-listen-audio.py website/public/audio/
git status   # 应是 1 脚本改动 + 9 个 mp3 更新
git commit -m "fix(docsite): 听书「行」多音字 háng->同音航, 重生成9篇mp3

清洗脚本对 一行行/画一行/第几行 等 háng 语境的「行」替换为同音字「航」,
让 TTS 读对(原读成 xíng)。不碰 执行/运行/行为(读 xíng 正确)。正文不变。

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: AudioPlayer 加滚动悬浮 mini 播放器

**Files:**
- Modify: `website/components/AudioPlayer.vue`

**设计：** 顶部播放器条用 IntersectionObserver 监测是否在视口内。当它**滚出视口 且 正在播放**时，在右下角渲染一个 `position:fixed` 的 mini 条（播放/暂停 + 进度 + 时间），复用同一 audio 元素与状态（同一组件内，不新建 audio）。滚回视口或暂停后 mini 条消失（暂停后可保留以便恢复——这里取「滚出即显示（无论播放与否），但只在播放过至少一次后才显示」更实用；简化为：滚出视口就显示 mini 条，方便随时暂停/继续）。

- [ ] **Step 1: 给 AudioPlayer 加视口检测与 mini 条**

修改 `website/components/AudioPlayer.vue`。

(a) `<script setup>` 顶部 import 加 `onMounted, onUnmounted` 已有；新增视口状态。在 `const audio = ref(null)` 附近加：
```js
const rootEl = ref(null)        // 顶部播放器条根元素
const inView = ref(true)        // 顶部条是否在视口
let io = null
const started = ref(false)      // 是否已开始播放过（避免没播就弹 mini）
```

(b) toggle() 里开始播放时标记 started：把 `if (a.paused) { a.play(); playing.value = true }` 改为
```js
  if (a.paused) { a.play(); playing.value = true; started.value = true }
```

(c) onMounted 里建 IntersectionObserver；onUnmounted 里断开。把现有 onMounted/onUnmounted 改为：
```js
onMounted(() => {
  if (audio.value) audio.value.playbackRate = rate.value
  if (rootEl.value && typeof IntersectionObserver !== 'undefined') {
    io = new IntersectionObserver(
      ([e]) => { inView.value = e.isIntersecting },
      { threshold: 0 }
    )
    io.observe(rootEl.value)
  }
})
onUnmounted(() => {
  if (audio.value) audio.value.pause()
  if (io) io.disconnect()
})
```

(d) 计算是否显示 mini 条：在 computed 区加
```js
const showMini = computed(() => started.value && !inView.value)
```

(e) template：给顶部根 div 加 ref，并在末尾加 mini 条。把 `<template>` 改为（在原 `.ap` div 加 `ref="rootEl"`，audio 元素留在原 .ap 内不动；新增 mini 浮层，mini 里的按钮/进度复用同样的 toggle/seek/fmt）：
```vue
<template>
  <div class="ap" ref="rootEl">
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

  <Teleport to="body">
    <Transition name="mini-fade">
      <div class="ap-mini" v-if="showMini">
        <button class="play" @click="toggle" :aria-label="playing ? '暂停' : '播放'">
          <span v-if="!playing">▶</span><span v-else>❚❚</span>
        </button>
        <span class="mlabel">听书</span>
        <div class="bar" @click="seek"><div class="fill" :style="{ width: progress + '%' }"></div></div>
        <span class="time">{{ fmt(cur) }}</span>
      </div>
    </Transition>
  </Teleport>
</template>
```

(f) `<style scoped>` 末尾加 mini 样式：
```css
.ap-mini {
  position: fixed; right: 1.2rem; bottom: 1.2rem; z-index: 100;
  display: flex; align-items: center; gap: 0.6rem;
  padding: 0.5rem 0.8rem; width: min(320px, 80vw);
  border: 1px solid #00d4aa; border-radius: 12px;
  background: var(--vp-c-bg-elv, var(--vp-c-bg-soft));
  box-shadow: 0 6px 20px rgba(0,0,0,0.35);
}
.ap-mini .play {
  width: 2rem; height: 2rem; font-size: 0.8rem; flex-shrink: 0;
  display: flex; align-items: center; justify-content: center;
  border: 1px solid #00d4aa; background: transparent; color: #00d4aa; border-radius: 8px; cursor: pointer;
}
.ap-mini .play:hover { background: rgba(0,212,170,0.12); }
.ap-mini .mlabel { flex-shrink: 0; font-size: 0.85rem; color: var(--vp-c-text-2); white-space: nowrap; }
.ap-mini .bar { flex: 1; min-width: 40px; height: 6px; background: var(--vp-c-bg); border-radius: 3px; cursor: pointer; overflow: hidden; }
.ap-mini .fill { height: 100%; background: #00d4aa; border-radius: 3px; }
.ap-mini .time { flex-shrink: 0; font-size: 0.78rem; color: var(--vp-c-text-3); font-family: var(--vp-font-family-mono); white-space: nowrap; }
.mini-fade-enter-active, .mini-fade-leave-active { transition: opacity 0.25s ease, transform 0.25s ease; }
.mini-fade-enter-from, .mini-fade-leave-to { opacity: 0; transform: translateY(10px); }
```

注意：`.ap-mini .play` 等用 Teleport 到 body，scoped 的 `data-v` 属性仍会带上（scoped 对 Teleport 内容生效），样式能命中。若验证发现样式未生效（Teleport scoped 偶有问题），改用 `:deep()` 或给 mini 一个独立非 scoped 处理——先按上面写，验证再说。

- [ ] **Step 2: build 验证**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba/website
npx vitepress build 2>&1 | tail -6
rm -rf .vitepress/dist .vitepress/cache
```
Expected: build 成功无死链。

- [ ] **Step 3: 提交**

```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git add website/components/AudioPlayer.vue
git commit -m "feat(docsite): 听书播放器滚动悬浮 mini 条(滚出顶部后右下角常驻)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: 部署 + 验证（控制器做悬浮交互验证）

- [ ] **Step 1: 合并 push**
```bash
cd /Users/hamber/development/repo/gba/libretro-mgba
git checkout master
git merge --no-ff feat/audio-fixes -m "Merge: 听书多音字修复 + 悬浮 mini 播放器

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git branch -d feat/audio-fixes
git push origin master
```

- [ ] **Step 2: 跟踪 Actions 到成功**
```bash
gh run list --workflow=docs.yml --limit 2
```

- [ ] **Step 3: 线上验证（控制器：headless 模拟滚动看 mini 条 + 抽听重生成的篇）**
- 页面顶部播放器在、mp3 src 带 base 前缀且 200。
- headless 滚动后 `.ap-mini` 出现（需先触发 started，模拟点击播放再滚动）。
- 抽查 intro.mp3 时长正常。
