# The dongle game

A small game engine that runs on the Totem dongle's 128×64 screen and is
played with the keyboard itself — left half moves you left, right half moves
you right, and what counts as "a move" gets harder as you climb the rungs.

The point is not only that it's a game. Every prompt it shows you is a key or
a chord on your own layout, and every hit and miss is a data point about which
ones your hands actually know.

## Why it lives here and not in the shield

`config/game/` is **portable C**: no ZMK, no Zephyr, no LVGL. It draws into a
plain 1 KB framebuffer laid out exactly like the dongle's LVGL canvas —
128 px wide, 16 bytes per row, most-significant bit leftmost — so the finished
frame can be handed straight to the screen.

That buys the thing that matters: the same sources compile natively on the Mac
against a terminal harness. Finding out whether a game is fun is
iteration-bound, not code-bound, and a rebuild here takes two seconds instead
of the five minutes a build-and-flash cycle costs.

## Layout

```
include/totem_game.h   the whole API: framebuffer, font, input, engine, games
src/fb.c               framebuffer and text rendering
src/font5x7.c          generated — do not edit
src/engine.c           prompts, scoring, the rung ladder, the auto-exit
src/game_drift.c       Drift, Catch, Balance
src/game_catch.c
src/game_balance.c
sim/sim.c              the terminal harness, and the autoplay sweep
web/                   the browser harness (emscripten)
tools/mkfont.py        glyph art → src/font5x7.c
```

## Playing it on the Mac

Two harnesses. Both compile the same sources; neither has any game logic of
its own.

### In the browser — the one to use

```sh
cd web
nix shell nixpkgs#emscripten -c make    # once, or whenever the C changes
open index.html
```

`web/game.js` is the engine and all the games compiled to WebAssembly with
emscripten, embedded in one file so the page works straight from disk. The
page draws the dongle's exact 128×64 framebuffer at six times size, with a
game picker, a rung picker and a stats readout. Play it with the Totem itself:
home-row mods work, so rung 3 is real here. Keys `1` `2` `3` switch game,
`F1`–`F3` switch rung, `Enter` restarts.

The browser can't take Gui chords without triggering its own shortcuts, so
rung 3 draws from Ctrl, Alt and Shift (`g.mod_pool`).

### In a terminal — for headless measurement

```sh
cd sim
make
./sim -g 1 -r 2     # -g picks the game (0 Drift, 1 Catch, 2 Balance), -r the rung
```

The terminal renderer needs a window **at least 128 columns by 34 rows** and a
font whose half-block characters (`▀ ▄ █`) join up; in the stock Terminal.app
they don't, and at 24 rows the frame scrolls. It exists for the autoplay sweep
below, not for playing.

### Measuring instead of guessing

`-a` runs a perfect player headlessly, using the game's `hint()`, and prints
how long it survived. `-i` sets how often that player can answer a prompt,
which is the one number that separates a beginner from a fluent one:

```sh
for r in 1 2 3; do for i in 200 350 500 750 1100; do ./sim -a -q -r $r -i $i -s 11; done; done
```

That sweep is how the corridor width and scroll speed were tuned. A run should
land somewhere between ten seconds and a minute and a half at the reaction
speed the rung expects — long enough to get into it, short enough that dying
isn't a loss.

## The rungs

| Rung | Prompt | What it trains |
| --- | --- | --- |
| 1 `RUNG_HAND` | `<ANY` / `ANY>` | Which hand is which. The warm-up. |
| 2 `RUNG_LETTER` | a letter, e.g. `T` | Where each letter lives, without looking. |
| 3 `RUNG_CHORD` | a mark and a letter, e.g. `^T` | Home-row mods, cross-handed. |
| 4 `RUNG_COMBO` | a combo | Firmware only — the combos you actually use. |

The modifier marks are `^` Ctrl, `~` Alt, `+` Shift, `*` Gui. The modifier
always comes from the hand opposite the letter, because that is how you'd play
it for real.

Harder rungs get a **wider, slower** corridor, not a tighter one. A chord costs
most of a second to find; the geometry has to give that second back or the
ladder stops being playable. The difficulty up there is the input, not the
walls.

## The games

Three, chosen to feel as different from each other as three games sharing one
input scheme can, so that comparing them tells us something.

### Drift — momentum

A corridor scrolls down; your ship sits near the bottom and has momentum and
nothing else. One hit is worth about eight pixels of travel, spent over about
130 ms, so crossing the screen is a sequence of presses rather than one. A
wrong key wobbles you — enough that guessing costs you, little enough that you
can recover. You die when you touch a wall.

### Catch — discrete

Five lanes. Things fall, one at a time, and you have to be underneath when
they land. A hit moves the basket exactly one lane — no momentum, no glide —
so the game is entirely "which lane, and how fast can you get there". A wrong
key stuns the basket for a third of a second. Three lives; it speeds up as
you go.

### Balance — no death

A ball on a beam, with a wind that keeps leaning on it. Thirty seconds on the
clock; every quarter-second the ball spends in the marked middle zone scores,
and falling off costs two seconds while it resets. Nothing kills you and the
round always ends on time, which makes it the cleanest measure of one thing:
how quickly you answer the prompt on the side the ball is drifting away from.

### What they share

Two prompts are on screen at once, one per corner, and you choose which to
answer. That is deliberate: if the game told you which key to press, it would
be a typing test. Choosing which key to press is what makes it a game.

Every game gets harder as it goes and easier on the higher rungs, for the same
reason: the difficulty on the high rungs is the input, and the geometry has to
give back the time a chord costs.

## Adding a game

Fill in a `struct game_def` and hand it to `game_start()`. The engine owns the
prompts, the scoring, the misses and the auto-exit; a game never sees a
keystroke, only `act(g, ±1)` and `miss(g)`. State lives in `g->scene`, which is
`GAME_SCENE_BYTES` of scratch you can cast to your own struct — no allocation,
because there is none to have on the dongle.

## Auto-exit

Game mode swallows keystrokes before they reach the Mac, so it must never be
possible to be stuck in it. After `GAME_IDLE_ASK_MS` of no keys the engine puts
up **STILL PLAYING?**; any key answers it. `GAME_ASK_GRACE_MS` of silence after
that sets `game_wants_exit()`, and the firmware drops back to the status
screen. The world freezes while the question is up — being asked whether you're
there must not be able to kill you.
