/*
 * Drift — the first game.
 *
 * A corridor scrolls down the screen and your ship sits near the bottom. The
 * ship has momentum and nothing else: the only way to move it is to satisfy
 * the prompt on the side you want to go. Easy rungs ask for a hand, hard rungs
 * ask for a cross-handed chord, so at the top of the ladder every pixel of
 * movement is a chord you had to find without looking.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "totem_game.h"

#define SHIP_ROW (GAME_PLAY_H - 9)
#define SHIP_HALF 2

/* Physics runs on a fixed step so the feel doesn't change with frame rate —
   the dongle and the simulator must play identically. */
#define STEP_MS 16
#define SUB 16 /* sub-pixel units per pixel */

/* Velocity decays by 1/8 per step, so a single impulse spends itself over
   about eight steps (130 ms) and carries the ship IMPULSE * 8 sub-pixels in
   total. At one pixel per step that is eight pixels a hit: enough that you
   feel the glide, few enough that crossing the screen is a sequence of
   presses rather than one. */
#define FRICTION_NUM 7
#define FRICTION_DEN 8
#define IMPULSE (1 * SUB)
#define VX_MAX (3 * SUB)

#define HW_START 24
#define HW_MIN 7

struct drift {
    uint8_t cx[GAME_PLAY_H]; /* corridor centre, per screen row */
    uint8_t hw[GAME_PLAY_H]; /* corridor half-width, per screen row */
    int16_t x;               /* ship position, sub-pixels */
    int16_t vx;              /* ship velocity, sub-pixels per step */
    int8_t wander;           /* which way the corridor is currently bending */
    uint8_t bend_left;       /* steps left before the bend changes */
    uint16_t acc;            /* leftover milliseconds */
    uint16_t scroll;         /* steps since the last row advanced */
    uint16_t rows;           /* rows travelled — the distance */
    uint8_t flash;           /* frames left of the miss flash */
};

/* The scene buffer is fixed and there is no allocator on the dongle, so this
   has to fail at compile time rather than at 3 a.m. */
_Static_assert(sizeof(struct drift) <= GAME_SCENE_BYTES, "drift state does not fit g->scene");

static struct drift *D(struct game *g) {
    return (struct drift *)g->scene;
}

/* The corridor tightens with distance. The harder rungs get a wider corridor,
   not a narrower one: a chord costs you most of a second to find, so the game
   has to give that second back somewhere or the ladder stops being playable.
   The difficulty on the high rungs is the input, not the geometry. */
static int target_hw(struct game *g) {
    struct drift *d = D(g);
    int hw = HW_START - (int)(d->rows / 90) + (g->rung - RUNG_LETTER) * 4;

    if (hw < HW_MIN) {
        hw = HW_MIN;
    }
    if (hw > HW_START + 8) {
        hw = HW_START + 8;
    }
    return hw;
}

static void new_row(struct game *g) {
    struct drift *d = D(g);
    int hw = d->hw[1];
    int want = target_hw(g);
    int cx;

    if (hw > want) {
        hw--;
    } else if (hw < want) {
        hw++;
    }

    if (d->bend_left == 0) {
        /* -1, 0 or +1: a straight stretch is as much a part of the rhythm as
           a bend, so it gets its own share. */
        d->wander = (int8_t)((int)game_rand(g, 3) - 1);
        d->bend_left = (uint8_t)(6 + game_rand(g, 18));
    }
    d->bend_left--;

    cx = (int)d->cx[1] + d->wander;
    if (cx < hw + 1) {
        cx = hw + 1;
        d->wander = 1;
    }
    if (cx > GAME_W - hw - 2) {
        cx = GAME_W - hw - 2;
        d->wander = -1;
    }

    d->cx[0] = (uint8_t)cx;
    d->hw[0] = (uint8_t)hw;
}

static void drift_start(struct game *g) {
    struct drift *d = D(g);

    memset(d, 0, sizeof(*d));
    for (int r = 0; r < GAME_PLAY_H; r++) {
        d->cx[r] = GAME_W / 2;
        d->hw[r] = HW_START;
    }
    d->x = (GAME_W / 2) * SUB;
}

static void drift_act(struct game *g, int dir) {
    struct drift *d = D(g);

    d->vx += (int16_t)(dir * IMPULSE);
    if (d->vx > VX_MAX) {
        d->vx = VX_MAX;
    }
    if (d->vx < -VX_MAX) {
        d->vx = -VX_MAX;
    }
}

static void drift_miss(struct game *g) {
    struct drift *d = D(g);

    /* A wrong key isn't free: it wobbles the ship. Small enough to recover
       from, big enough that you stop guessing. */
    d->vx += (int16_t)(game_rand(g, 2) ? IMPULSE / 2 : -IMPULSE / 2);
    d->flash = 4;
}

/* Scroll speed climbs with distance: 1 row every 5 steps down to every 3, so
   the corridor gets both tighter and faster. Each rung above letters adds a
   step, for the same reason the corridor is wider up there. */
static void scroll(struct game *g) {
    struct drift *d = D(g);
    int period = (d->rows < 400 ? 5 : (d->rows < 900 ? 4 : 3)) + (int)g->rung - RUNG_LETTER;

    if (period < 2) {
        period = 2;
    }
    if (++d->scroll < (uint16_t)period) {
        return;
    }

    d->scroll = 0;
    memmove(&d->cx[1], &d->cx[0], GAME_PLAY_H - 1);
    memmove(&d->hw[1], &d->hw[0], GAME_PLAY_H - 1);
    new_row(g);
    d->rows++;
    if (d->rows % 16 == 0) {
        g->score++; /* surviving pays, slowly */
    }
}

static void step(struct game *g) {
    struct drift *d = D(g);
    int px;

    d->x = (int16_t)(d->x + d->vx);
    d->vx = (int16_t)(d->vx * FRICTION_NUM / FRICTION_DEN); /* coast, don't stop dead */

    if (d->x < 0) {
        d->x = 0;
        d->vx = 0;
    }
    if (d->x > (GAME_W - 1) * SUB) {
        d->x = (GAME_W - 1) * SUB;
        d->vx = 0;
    }

    scroll(g);

    px = d->x / SUB;
    if (px - SHIP_HALF < (int)d->cx[SHIP_ROW] - (int)d->hw[SHIP_ROW] ||
        px + SHIP_HALF > (int)d->cx[SHIP_ROW] + (int)d->hw[SHIP_ROW]) {
        g->over = true;
    }
}

static void drift_tick(struct game *g, uint32_t dt) {
    struct drift *d = D(g);

    if (dt > 200) {
        dt = 200; /* don't fast-forward through a stall */
    }
    d->acc = (uint16_t)(d->acc + dt);
    while (d->acc >= STEP_MS && !g->over) {
        d->acc -= STEP_MS;
        step(g);
    }
    if (d->flash) {
        d->flash--;
    }
}

static void drift_draw(struct game *g, struct game_fb *f) {
    struct drift *d = D(g);
    int px = d->x / SUB;
    char buf[12];

    for (int r = 0; r < GAME_PLAY_H; r++) {
        int l = (int)d->cx[r] - (int)d->hw[r];
        int rr = (int)d->cx[r] + (int)d->hw[r];

        fb_px(f, l, r, 1);
        fb_px(f, rr, r, 1);

        /* A dash every few rows outside the walls: without it a slow scroll
           looks like a still picture. */
        if (((r + d->rows) & 7) == 0) {
            fb_px(f, l - 3, r, 1);
            fb_px(f, rr + 3, r, 1);
        }
    }

    /* The ship: a little arrowhead, drawn XOR so it stays visible against a
       wall it is about to hit. */
    for (int i = 0; i <= SHIP_HALF; i++) {
        fb_hline(f, px - i, SHIP_ROW - SHIP_HALF + i + 2, i * 2 + 1, 2);
    }
    fb_px(f, px, SHIP_ROW - SHIP_HALF, 2);

    snprintf(buf, sizeof(buf), "%um", d->rows / 8);
    fb_text(f, 2, 2, buf, 1, 1);

    if (d->flash) {
        fb_frame(f, 0, 0, GAME_W, GAME_PLAY_H, 2);
    }
}

static int drift_hint(struct game *g) {
    struct drift *d = D(g);
    /* Aim a little ahead of the ship, because the corridor it will meet is the
       one a few rows up, not the one it is in. */
    int aim = (int)d->cx[SHIP_ROW > 6 ? SHIP_ROW - 6 : 0];
    int px = d->x / SUB;
    /* Where the ship would stop if nothing more were pressed. */
    int rest = px + (d->vx * FRICTION_DEN / SUB) / (FRICTION_DEN - FRICTION_NUM);

    if (rest < aim - 2) {
        return 1;
    }
    if (rest > aim + 2) {
        return -1;
    }
    return 0;
}

const struct game_def game_drift = {
    .name = "DRIFT",
    .start = drift_start,
    .act = drift_act,
    .miss = drift_miss,
    .tick = drift_tick,
    .draw = drift_draw,
    .hint = drift_hint,
};
