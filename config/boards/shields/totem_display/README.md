# The Totem dongle screen

The dongle drives a 128×64 SH1106 OLED. Instead of a stack of LVGL widgets,
the whole screen is one canvas drawn pixel by pixel in
[`widgets/totem_screen.c`](widgets/totem_screen.c).

```
┌─────────────────────────────────────────────┐
│ 82% ◣            ● ▐Nav▌            ◢ 100%  │   battery · recorder · layer
│                                             │
│      hello wo⌘C  ⌘V   ✦4        ▐ ⌥⇧ ▌      │   what the keyboard sent
│                                             │
│                Area to clipbd               │   …in words
└─────────────────────────────────────────────┘
```

- **Battery corners.** One gauge per half, leaning outward, draining toward its
  own corner like a health bar. The percentage is drawn as an inversion so it
  stays readable over both the full and the empty part of the gauge. A half
  that has not reported yet shows `--`.
- **The middle.** The active layer in a badge, with a dot to its left while the
  key recorder is running (hollow and blinking when nothing on the host is
  listening). The badge switches to narrow letters rather than growing into the
  batteries.
- **The line.** Everything the keyboard has actually sent, newest on the right.
  Typed characters run together as text; anything with a modifier on it becomes
  a chip — `⌘C`, `✦4` — and a repeat counts up (`⌫ ×3`) instead of filling the
  line. New presses slide in from the right with a small bounce, and the line
  dissolves into a dither over the last 10 px at each end.
- **Held modifiers** sit in an inverted chip at the right-hand edge: they are
  what is about to happen, not part of the stream. Ctrl+Shift+Cmd is this
  board's Hyper key, so it shows as one `✦` rather than three symbols.
- **The caption** names the newest thing in plain language, typed out a letter
  at a time.

## Making it say what your keyboard does

[`widgets/key_names.c`](widgets/key_names.c) is the one file to edit. It holds
three tables:

| Table | What it does |
| --- | --- |
| `key_labels[]` | how a bare key is drawn and spoken: `⏎` / "Return" |
| `shortcuts[]` | modifier + key → chip and caption: `⌘⇧4` → "Area shot" |
| `punct_keys[]`, `digits_*` | the character a key types, which is what makes text read as text |

The captions are macOS-flavoured, because this keyboard is. Swap `⌘` for `Ctrl`
and rename the entries and the screen will speak your system instead.

Two more knobs:

- `totem_key_char()` decides what counts as *typing* rather than a shortcut.
  Only Shift is allowed; every other modifier makes a chip.
- Dictation on this board is two taps of Ctrl, which never reaches the host at
  all, so `totem_screen.c` spots the double tap itself and shows a `🎤`. Delete
  that block if your Ctrl is just Ctrl.

## Symbols

`⌘ ⌥ ⇧ ⌃ ⏎ ⇥ ⌫ ⌦ ␣ ← → ↑ ↓ ⏯ ⏭ ⏮ 🔊 🔉 🔇 ✦ · × 🎤` are hand-drawn 10 px
bitmaps in [`widgets/totem_symbols.c`](widgets/totem_symbols.c), looked up by
codepoint whenever a label contains one — the stock fonts render them far too
large for a 64 px screen, if they have them at all. Add one by appending a
bitmap and its codepoint to `totem_symbols[]`; labels stay plain UTF-8 strings.

## Options

| Kconfig | Default | Effect |
| --- | --- | --- |
| `ZMK_TOTEM_DISPLAY_SCREEN` | `y` | this screen; turn it off for the original widget layout |
| `ZMK_TOTEM_DISPLAY_SWAP_BATTERIES` | `n` | swap which corner each half is shown in |

The layout constants — the band the line lives in, the fade width, the
animation timings — are all `#define`s at the top of `totem_screen.c`.
