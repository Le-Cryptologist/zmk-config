/*
 * Catch — the second game, built to feel nothing like Drift.
 *
 * Five lanes. Things fall, one at a time, and you have to be underneath when
 * they land. A hit moves the basket exactly one lane, so there is no momentum
 * to manage: the whole game is "which lane, and how fast can you get there".
 * Three lives, faster as you go.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "totem_game.h"

#define LANES 5
#define LANE_W (GAME_W / LANES) /* 25 px, with 3 px spare on the right */
#define LANE_X0 ((GAME_W - LANE_W * LANES) / 2)
#define BASKET_ROW (GAME_PLAY_H - 7)
#define BASKET_W 15
#define STEP_MS 16
#define SUB 16
#define STUN_MS 350
#define LIVES 3

struct catch_state {
    int8_t lane;      /* where the basket is */
    int8_t obj_lane;  /* where the falling thing is */
    int16_t obj_y;    /* sub-pixels */
    int16_t speed;    /* sub-pixels per step */
    uint16_t acc;
    uint16_t stun;    /* ms left of the wrong-key stun */
    uint16_t caught;
    uint8_t lives;
    uint8_t flash;    /* frames of "lost a life" */
    uint8_t pop;      /* frames of "caught it" */
};

_Static_assert(sizeof(struct catch_state) <= GAME_SCENE_BYTES, "catch state too big");

static struct catch_state *S(struct game *g) {
    return (struct catch_state *)g->scene;
}

static int lane_cx(int lane) {
    return LANE_X0 + lane * LANE_W + LANE_W / 2;
}

static void spawn(struct game *g) {
    struct catch_state *s = S(g);
    int lane;

    /* Never the lane you're already in: the point is to move. Never more than
       two lanes away early on, so the first few are winnable at any rung. */
    do {
        int reach = s->caught < 8 ? 2 : LANES;
        int lo = s->lane - reach < 0 ? 0 : s->lane - reach;
        int hi = s->lane + reach >= LANES ? LANES - 1 : s->lane + reach;

        lane = lo + (int)game_rand(g, (uint32_t)(hi - lo + 1));
    } while (lane == s->lane);

    s->obj_lane = (int8_t)lane;
    s->obj_y = 0;
}

static void catch_start(struct game *g) {
    struct catch_state *s = S(g);

    memset(s, 0, sizeof(*s));
    s->lane = LANES / 2;
    s->lives = LIVES;
    /* Base speed: about 2.6 s from top to basket at rung 2. The harder rungs
       start slower, as in Drift, because their prompts take longer. */
    s->speed = (int16_t)(SUB * 3 / 8 - (g->rung - RUNG_LETTER) * 2);
    if (s->speed < 3) {
        s->speed = 3;
    }
    spawn(g);
}

static void catch_act(struct game *g, int dir) {
    struct catch_state *s = S(g);

    if (s->stun) {
        return;
    }
    s->lane = (int8_t)(s->lane + dir);
    if (s->lane < 0) {
        s->lane = 0;
    }
    if (s->lane >= LANES) {
        s->lane = LANES - 1;
    }
}

static void catch_miss(struct game *g) {
    S(g)->stun = STUN_MS;
}

static void catch_tick(struct game *g, uint32_t dt) {
    struct catch_state *s = S(g);

    if (dt > 200) {
        dt = 200;
    }
    if (s->stun) {
        s->stun = (uint16_t)(s->stun > dt ? s->stun - dt : 0);
    }
    if (s->flash) {
        s->flash--;
    }
    if (s->pop) {
        s->pop--;
    }

    s->acc = (uint16_t)(s->acc + dt);
    while (s->acc >= STEP_MS && !g->over) {
        s->acc -= STEP_MS;
        s->obj_y = (int16_t)(s->obj_y + s->speed);

        if (s->obj_y / SUB >= BASKET_ROW - 2) {
            if (s->obj_lane == s->lane) {
                s->caught++;
                g->score += 5;
                s->pop = 6;
                /* A touch faster every catch, up to about twice the start. */
                if (s->speed < SUB * 3 / 4) {
                    s->speed = (int16_t)(s->speed + (s->caught % 2 ? 1 : 0));
                }
            } else {
                s->lives--;
                s->flash = 6;
                g->streak = 0;
                if (s->lives == 0) {
                    g->over = true;
                    return;
                }
            }
            spawn(g);
        }
    }
}

static void catch_draw(struct game *g, struct game_fb *f) {
    struct catch_state *s = S(g);
    int bx = lane_cx(s->lane);
    int ox = lane_cx(s->obj_lane);
    int oy = s->obj_y / SUB;
    char buf[8];

    /* Lane markers: a dotted line between lanes so the target reads at a
       glance without walling the screen off. */
    for (int l = 1; l < LANES; l++) {
        int x = LANE_X0 + l * LANE_W;

        for (int y = 2; y < GAME_PLAY_H; y += 4) {
            fb_px(f, x, y, 1);
        }
    }

    /* The falling thing: a diamond. */
    for (int i = -2; i <= 2; i++) {
        int w = 2 - (i < 0 ? -i : i);

        fb_hline(f, ox - w, oy + i, w * 2 + 1, 1);
    }

    /* The basket: a cup, drawn hollow when stunned so the penalty is visible
       and solid-bottomed otherwise. It jumps a pixel when it catches. */
    {
        int y = BASKET_ROW - (s->pop ? 1 : 0);

        fb_vline(f, bx - BASKET_W / 2, y - 3, 4, 1);
        fb_vline(f, bx + BASKET_W / 2, y - 3, 4, 1);
        fb_hline(f, bx - BASKET_W / 2, y, BASKET_W, 1);
        if (!s->stun) {
            fb_hline(f, bx - BASKET_W / 2 + 1, y + 1, BASKET_W - 2, 1);
        }
    }

    /* Lives, top right. */
    for (int i = 0; i < s->lives; i++) {
        fb_rect(f, GAME_W - 4 - i * 5, 2, 3, 3, 1);
    }

    snprintf(buf, sizeof(buf), "%u", s->caught);
    fb_text(f, 2, 2, buf, 1, 1);

    if (s->flash) {
        fb_frame(f, 0, 0, GAME_W, GAME_PLAY_H, 2);
    }
}

static int catch_hint(struct game *g) {
    struct catch_state *s = S(g);

    return s->obj_lane > s->lane ? 1 : s->obj_lane < s->lane ? -1 : 0;
}

const struct game_def game_catch = {
    .name = "CATCH",
    .start = catch_start,
    .act = catch_act,
    .miss = catch_miss,
    .tick = catch_tick,
    .draw = catch_draw,
    .hint = catch_hint,
};
