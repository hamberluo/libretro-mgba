#!/usr/bin/env python3
"""读 website/guide/*.md，清洗成朗读文本，用 edge-tts 生成 mp3 到 website/public/audio/。
用法：
  python3 tools/build-listen-audio.py            # 生成全部 10 篇
  python3 tools/build-listen-audio.py ep05-timing # 只生成指定篇
"""
import asyncio, re, sys
from pathlib import Path
import edge_tts

WEB = Path(__file__).resolve().parent.parent
GUIDE = WEB / "guide"
OUT = WEB / "public" / "audio"
OUT.mkdir(parents=True, exist_ok=True)

VOICE = "zh-CN-XiaoxiaoNeural"
RATE = "+30%"

SLUGS = ["intro", "ep02-cpu", "ep03-thumb", "ep04-memory", "ep05-timing",
         "ep06-dma", "ep07-ppu", "ep08-bios", "ep09-audio", "ep10-savestate"]


def clean(md):
    md = re.sub(r"```.*?```", "\n这里有一段源码，详见网页。\n", md, flags=re.DOTALL)
    out = []
    for line in md.splitlines():
        s = line.rstrip()
        if not s.strip():
            out.append("")
            continue
        if re.match(r"^\s*<[A-Z][A-Za-z]+\s*/?>", s):
            out.append("这里有一个交互演示，可以在网页上动手试试。")
            continue
        if "↗" in s and s.lstrip().startswith(">"):
            continue
        if re.match(r"^\s*\|.*\|\s*$", s):
            continue
        s = re.sub(r"^#{1,6}\s*", "", s)
        s = re.sub(r"^\s*[-*]\s+", "", s)
        s = re.sub(r"\*\*([^*]+)\*\*", r"\1", s)
        s = re.sub(r"`([^`]+)`", r"\1", s)
        s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
        out.append(s)
    text = "\n".join(out)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


async def synth(slug):
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
