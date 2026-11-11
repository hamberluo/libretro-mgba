# GXA 1.0: external audio for Game Boy Advance ROMs

GXA (GBA eXternal Audio) lets a GBA ROM play audio tracks stored outside the
ROM, at any quality and length. Typical uses are voiced dialogue, arranged
music, and high-quality sound effects for ROM hacks and homebrew. The SNES
has MSU-1 for this; GXA does the same job for the GBA.

A GXA ROM stays a normal GBA ROM. On hardware, and on emulators without GXA,
the registers are not there, the handshake fails, and the game runs without
the extra audio.

The key words MUST, SHOULD and MAY are used as in RFC 2119.

## 1. Registers

The registers are at `0x04FFFA00`–`0x04FFFA7F`. Real hardware has nothing in
the I/O region above `0x04000400`, and this range avoids the 64 KiB mirrors of
`0x04000800`.

Every register is 16 bits wide, and the byte order is little-endian. A ROM
MUST use 16-bit or 32-bit accesses. The result of an 8-bit access is
undefined. A 32-bit store writes the low halfword first and then the high
halfword, so storing `(1 << 16) | track` at a channel's `TRACK` selects the
track and then plays it.

### Global registers

| Offset | Name | Access | Reset value | Meaning |
|---|---|---|---|---|
| `0x00` | `ID0` | R | `0x5847` | "GX" in memory |
| `0x02` | `ID1` | R | `0x3141` | "A1" in memory |
| `0x04` | `HANDSHAKE` | RW | | After v is written, reads `v ^ 0x5847` |
| `0x06` | `VERSION` | R | `0x0100` | High byte is the major version, low byte the minor |
| `0x08` | `STATUS` | R | | Bit 0: a pack is loaded. Bits 8–11: the channel count (4) |
| `0x0A` | `GAMEVOL` | RW | `256` | Volume of the GBA's own sound, 0–256 (256 = unchanged) |
| `0x0C` | `DUCK` | RW | `256` | While any channel is playing, the GBA's own sound is scaled by `GAMEVOL × DUCK / 256` |

Values above 256 written to `GAMEVOL`, `DUCK` or `VOL` read back as 256.
`0x50`–`0x7F` is reserved: it reads 0 and ignores writes. A console reset
puts every register back to its reset value and stops every channel.

### Channel registers

There are 4 channels, n = 0–3. The registers of channel n are at
`0x10 + 0x10 × n`:

| Offset | Name | Access | Reset value | Meaning |
|---|---|---|---|---|
| `+0x0` | `TRACK` | RW | `0` | The track id, 0–65535, as defined by the pack's manifest |
| `+0x2` | `CTRL` | W | | A command (see below) |
| `+0x4` | `VOL` | RW | `256` | Channel volume, 0–256 |
| `+0x6` | `FADE` | RW | `0` | The ramp time in milliseconds for the next `VOL` write; 0 means at once |
| `+0x8` | `STATE` | R | `0` | Bit 0 playing, bit 1 paused, bit 2 ended, bit 3 missing, bit 4 error |
| `+0xC` | `POS` | R | `0` | 32 bits: milliseconds played since the last play command |

`CTRL` commands:

| Value | Command | Effect |
|---|---|---|
| 1 | play | Starts `TRACK` from the beginning, once |
| 2 | loop | Starts `TRACK` from the beginning and loops it, from the manifest's `loop` frame or from 0 |
| 3 | stop | Stops the channel; `STATE` reads 0 |
| 4 | pause | Only if playing: clears playing, sets paused, and holds `POS` |
| 5 | resume | Only if paused: clears paused and sets playing |

## 2. Detection

```c
#define GXA ((volatile uint16_t *) 0x04FFFA00)

GXA[2] = 0x1234;                        // HANDSHAKE
bool gxa = GXA[2] == (0x1234 ^ 0x5847);
bool pack = gxa && (GXA[4] & 1);        // STATUS: a pack for this ROM is loaded
```

Without GXA, a read returns whatever value the bus last held (open bus), so
the transformed value cannot match by accident. An implementation MUST answer
the handshake even when no pack is loaded; `STATUS` bit 0 then reads 0.

A ROM SHOULD treat "no GXA" and "no pack" the same way: it carries on without
the extra audio.

## 3. Playback

- **Starting a track.** A play or loop command for a track that the pack does
  not hold, or any play command while no pack is loaded, sets
  `STATE = missing | ended` at once. A track that cannot be decoded sets
  `error | ended`. A ROM that waits for ended therefore never hangs.
- **Ending.** When a play (not a loop) reaches the end of its track, `STATE`
  becomes `ended` and stays that way until the next command. `POS` keeps its
  final value.
- **Time.** Channels advance in emulated time, on the same clock as the GBA's
  own sound. Fast-forward, slow motion, pause, rewind and savestates therefore
  affect GXA exactly as they affect the game. An implementation SHOULD set
  ended within 20 ms of the track's length; the reference implementation is
  exact to one output sample.
- **Volume ramps.** A `VOL` write ramps linearly from the current volume to the
  new one over `FADE` milliseconds. When a ramp to 0 finishes, the channel
  stops and reads ended.
- **Mixing.** The output is the GBA's own sound, scaled by the game volume,
  plus every playing channel at its volume and its track's gain.
- **Muted output.** While the emulator mutes its sound (for example during
  fast-forward), channels MUST still advance and end on time. Decoding them
  MAY be skipped.

## 4. Packs

A pack is a zip file with the extension `.gxa`.

- Every entry MUST be stored, not compressed. The audio is already
  compressed, and stored entries can be read in place.
- `manifest.json` sits at the root.
- Audio files are Ogg Vorbis, mono or stereo, at any sample rate.

```json
{
  "spec": 1,
  "title": "Langrisser II voice",
  "author": "...",
  "version": "1.0",
  "license": "...",
  "match": [
    { "sha1": "0123456789abcdef0123456789abcdef01234567" },
    { "code": "ZL2J", "title": "LANGRISSER2", "version": [1, 4] }
  ],
  "tracks": {
    "1": { "file": "voice/0001.ogg" },
    "2": { "file": "bgm/title.ogg", "loop": 441000, "gain": -3 }
  }
}
```

- `spec` MUST be 1.
- `tracks` maps a decimal track id (0–65535) to an object:
  - `file`: the entry's path inside the zip.
  - `loop` (optional): the sample frame, in the file's own rate, that a loop
    command returns to.
  - `gain` (optional): a gain in dB applied to the track.
- `match` lists the ROMs the pack is for; any one rule is enough:
  - `sha1`: the SHA-1 of the whole ROM file.
  - `code` + `title`: the game code at `0xAC` and the title at `0xA0`.
    Trailing spaces and NULs in the title are ignored.
  - `version` (optional, with `code` + `title`): the header's version byte at
    `0xBC`, as a number (that version and later) or `[min, max]`, 0–255.

A pack matches fully by `sha1`, or by `code` + `title` with the ROM's version
in the rule's `version`. When it matches only by header (no `version`, or one
outside it), the ROM may be a different version than the one the pack was
made for. A frontend SHOULD tell the player so, and MAY still load it.

### Updating a game

A game has one pack. When the game's content changes, its author re-releases
the whole pack; players import the new one in place of the old.

- Track ids are the game's: once a released ROM plays an id, the id keeps its
  meaning. New audio takes new ids; audio that changes meaning (a rewritten
  line) takes a new id, and its old id is never reused.
- A ROM release bumps the header's version byte. A pack that still fits the
  new ROM names it in `version` (`"version": 1` covers every later one); a
  pack for the new content names the versions it was made for.
- A newer ROM with an older pack plays what the pack holds: the tracks it
  lacks read missing, and the ROM carries on as without GXA for those.

`gxa/tools/gxa_pack.py` builds a pack from a directory. It encodes WAV and
FLAC files, checks every rule above, and writes stored entries.

## 5. Frontends

- Loading a pack is the frontend's job. As a convention, a pack named like
  the ROM (`game.gba` → `game.gxa`) and placed beside it SHOULD be loaded
  automatically.
- GXA's state (registers and channel positions) SHOULD be saved with the
  machine's state.
  - When a state is loaded while a different pack is loaded, or no pack, its
    playing channels read ended.
  - A state saved without GXA state resets GXA (all channels stopped, every
    register at its reset value).
- During link play, only the first instance SHOULD provide GXA. The others
  fail the handshake.

## 6. Conformance

`gxa/test-rom/gxa-conformance.gba` and its pack `gxa-conformance.gxa` check:
- the handshake and `STATUS`;
- that a one-second play ends on time;
- missing tracks;
- loops;
- pause and resume;
- volume readback;
- the 32-bit store order.

Each case draws a row: green on pass, red on fail. A white row at the bottom
means the run is done. Automated runs read the result block at `0x02000100`:
- `+0x0`: "GXAR";
- `+0x4`: one bit per passed case;
- `+0x8`: 1 when done;
- `+0xC`: the number of cases.

## 7. Versions

Minor versions add registers or commands without changing existing ones. A
ROM that checks `VERSION`'s high byte for 1 keeps working.
