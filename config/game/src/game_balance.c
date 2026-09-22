/*
 * Balance — the third game, and the one with no way to die.
 *
 * A ball sits on a beam and the wind keeps trying to push it off. You push
 * back. Thirty seconds on the clock; every moment the ball spends in the
 * middle zone scores, and falling off costs you two seconds while it resets.
 * No scrolling, no lives: a fixed-length round that isolates one thing —
 * how quickly you can answer the prompt on the side the ball is drifting
 * away from.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "totem_game.h"

#define ROUND_MS 30000
#define RESET_MS 2000
#define STEP_MS 16
#define SUB 64 /* finer than the other games: the wind is a small number */
#define BEAM_Y (GAME_PLAY_H - 14)
#define BEAM_X0 8
#define BEAM_W (GAME_W - 2 * BEAM_X0)
#define ZONE_HW 12 /* half-width of the scoring zone, px */
#define BALL_R 3
#define IMPULSE (SUB * 5 / 4)
/* Wind is applied every step against a 1/16 drag, so it settles the ball at
   16 times its own strength: 16 or 32 px/s. One push is worth about 20 px. */
#define WIND_MAX 2

struct balance {
    int16_t x;       /* ball, sub-pixels */
    int16_t vx;      /* sub-pixels per step */
    int16_t wind;    /* sub-pixels per step, applied every step */
    uint16_t acc;
    uint16_t reset;  /* ms left of the off-the-edge reset */
    uint16_t gust;   /* steps until the wind changes */
    uint32_t elapsed;
    uint16_t in_zone_ms;
    uint8_t falls;
};

_Static_assert(sizeof(struct balance) <= GAME_SCENE_BYTES, "balance state too big");

static struct balance *B(struct game *g) {
    return (struct balance *)g->scene;
}

static void balance_start(struct game *g) {
    struct balance *b = B(g);

    memset(b, 0, sizeof(*b));
    b->x = (GAME_W / 2) * SUB;
}

static void balance_act(struct game *g, int dir) {
    struct balance *b = B(g);

    if (b->reset) {
        return;
    }
    b->vx = (int16_t)(b->vx + dir * IMPULSE);
}

static void balance_miss(struct game *g) {
    struct balance *b = B(g);

    /* A wrong key is a gust in the wind's own direction. */
    b->vx = (int16_t)(b->vx + (b->wind >= 0 ? IMPULSE / 2 : -IMPULSE / 2));
}

static void step(struct game *g) {
    struct balance *b = B(g);
    int px;

    if (b->gust == 0) {
        /* The wind leans one way for a while, then another. It is biased away
           from the centre, because a fair wind would be a boring one. */
        int lean = b->x < (GAME_W / 2) * SUB ? -1 : 1;
        int strength = 1 + (int)game_rand(g, WIND_MAX);

        b->wind = (int16_t)(game_rand(g, 4) ? lean * strength : -lean * strength);
        b->gust = (uint16_t)(20 + game_rand(g, 50));
        /* Harder rungs get gentler, longer gusts, for the same reason Drift's
           corridor is wider up there. */
        b->gust = (uint16_t)(b->gust + (g->rung - RUNG_LETTER) * 15);
        if (g->rung >= RUNG_CHORD && b->wind) {
            b->wind = (int16_t)(b->wind / 2 ? b->wind / 2 : b->wind);
        }
    }
    b->gust--;

    b->vx = (int16_t)(b->vx + b->wind);
    b->vx = (int16_t)(b->vx * 15 / 16);
    b->x = (int16_t)(b->x + b->vx);

    px = b->x / SUB;
    if (px < BEAM_X0 || px >= BEAM_X0 + BEAM_W) {
        b->reset = RESET_MS;
        b->falls++;
        b->vx = 0;
        g->streak = 0;
    } else if (px >= GAME_W / 2 - ZONE_HW && px <= GAME_W / 2 + ZONE_HW) {
        b->in_zone_ms = (uint16_t)(b->in_zone_ms + STEP_MS);
        if (b->in_zone_ms >= 250) {
            b->in_zone_ms = 0;
            g->score++;
        }
    }
}

static void balance_tick(struct game *g, uint32_t dt) {
    struct balance *b = B(g);

    if (dt > 200) {
        dt = 200;
    }
    b->elapsed += dt;
    if (b->elapsed >= ROUND_MS) {
        g->over = true;
        return;
    }

    if (b->reset) {
        b->reset = (uint16_t)(b->reset > dt ? b->reset - dt : 0);
        if (!b->reset) {
            b->x = (GAME_W / 2) * SUB;
            b->gust = 0;
        }
        return;
    }

    b->acc = (uint16_t)(b->acc + dt);
    while (b->acc >= STEP_MS && !b->reset) {
        b->acc -= STEP_MS;
        step(g);
    }
}

static void balance_draw(struct game *g, struct game_fb *f) {
    struct balance *b = B(g);
    int px = b->x / SUB;
    int left = ROUND_MS - (int)b->elapsed;
    char buf[8];

    /* The clock, as a bar across the top that empties. */
    fb_frame(f, 2, 2, GAME_W - 4, 4, 1);
    fb_rect(f, 3, 3, (GAME_W - 6) * left / ROUND_MS, 2, 1);

    /* The beam, with the scoring zone marked underneath it. */
    fb_hline(f, BEAM_X0, BEAM_Y, BEAM_W, 1);
    fb_hline(f, BEAM_X0, BEAM_Y + 1, BEAM_W, 1);
    fb_vline(f, BEAM_X0, BEAM_Y - 2, 4, 1);
    fb_vline(f, BEAM_X0 + BEAM_W - 1, BEAM_Y - 2, 4, 1);
    for (int x = GAME_W / 2 - ZONE_HW; x <= GAME_W / 2 + ZONE_HW; x += 2) {
        fb_px(f, x, BEAM_Y + 3, 1);
    }
    fb_px(f, GAME_W / 2, BEAM_Y + 4, 1);
    fb_px(f, GAME_W / 2, BEAM_Y + 5, 1);

    /* The wind: a streak of dashes at ball height, pointing the way it blows
       and as long as it is strong. */
    {
        int n = b->wind < 0 ? -b->wind : b->wind;
        int dir = b->wind < 0 ? -1 : 1;
        int y = BEAM_Y - 12;

        for (int i = 0; i < n * 2; i++) {
            int x = GAME_W / 2 + dir * (6 + i * 4);

            fb_hline(f, x, y, 2, 1);
        }
        if (n) {
            int tip = GAME_W / 2 + dir * (6 + n * 2 * 4);

            fb_px(f, tip, y, 1);
            fb_px(f, tip - dir, y - 1, 1);
            fb_px(f, tip - dir, y + 1, 1);
        }
    }

    if (b->reset) {
        /* Falling: the ball drops below the beam and the beam blinks. */
        int drop = (RESET_MS - b->reset) / 60;

        if (drop > 10) {
            drop = 10;
        }

        fb_rect(f, px - BALL_R, BEAM_Y + 2 + drop, BALL_R * 2 + 1, BALL_R * 2 + 1, 1);
        if ((b->reset / 150) & 1) {
            fb_hline(f, BEAM_X0, BEAM_Y, BEAM_W, 2);
        }
    } else {
        for (int i = -BALL_R; i <= BALL_R; i++) {
            int w = BALL_R - (i < 0 ? -i : i) / 2;

            fb_hline(f, px - w, BEAM_Y - BALL_R - 1 + i, w * 2 + 1, 1);
        }
    }

    snprintf(buf, sizeof(buf), "%u", b->falls);
    fb_text(f, GAME_W - 2 - fb_text_w(buf, 1), 8, buf, 1, 1);
}

static int balance_hint(struct game *g) {
    struct balance *b = B(g);
    /* Where the ball will settle if the wind held and nobody pressed. */
    int rest = b->x / SUB + (b->vx + b->wind * 16) / SUB;

    if (b->reset) {
        return 0;
    }
    if (rest < GAME_W / 2 - 3) {
        return 1;
    }
    if (rest > GAME_W / 2 + 3) {
        return -1;
    }
    return 0;
}

const struct game_def game_balance = {
    .name = "BALANCE",
    .start = balance_start,
    .act = balance_act,
    .miss = balance_miss,
    .tick = balance_tick,
    .draw = balance_draw,
    .hint = balance_hint,
};
