/*
 * The engine: everything the games share.
 *
 * It owns the two prompts, the scoring, the miss counting, and the auto-exit.
 * A game never sees a keystroke — it is told "a prompt on this side was
 * satisfied" or "that was a miss", which is what keeps the games small enough
 * to be worth writing several of.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "totem_game.h"

/* The Totem's Colemak-DH base layer, by hand. Only the letters: these are what
   prompts are made of. */
static const char LEFT_KEYS[] = "QWFPBARSTGZXCDV";
static const char RIGHT_KEYS[] = "JLUYMNEIOKH";

/* Home-row mods, in the order they sit on the keyboard: left A R S T is
   Ctrl Alt Shift Gui, right O I E N is the mirror. A chord prompt takes its
   modifier from the hand opposite the letter, which is how you'd play it. */
static const uint8_t LEFT_MOD_BITS[] = {GAME_MOD_CTRL, GAME_MOD_ALT, GAME_MOD_SHIFT,
                                        GAME_MOD_GUI};
static const uint8_t RIGHT_MOD_BITS[] = {GAME_MOD_CTRL, GAME_MOD_ALT, GAME_MOD_SHIFT,
                                         GAME_MOD_GUI};

/* One character each, because the font is 5x7 and a modifier glyph has to sit
   next to a letter without crowding it. */
static char mod_glyph(uint8_t bit) {
    switch (bit) {
    case GAME_MOD_CTRL:
        return '^';
    case GAME_MOD_ALT:
        return '~';
    case GAME_MOD_SHIFT:
        return '+';
    case GAME_MOD_GUI:
        return '*';
    default:
        return '?';
    }
}

int game_char_half(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (strchr(LEFT_KEYS, c) && c) {
        return GAME_LEFT;
    }
    if (strchr(RIGHT_KEYS, c) && c) {
        return GAME_RIGHT;
    }
    return -1;
}

uint32_t game_rand(struct game *g, uint32_t n) {
    /* xorshift32 — small, fast, and repeatable from a seed, which matters when
       we want the same run twice while tuning. */
    g->seed ^= g->seed << 13;
    g->seed ^= g->seed >> 17;
    g->seed ^= g->seed << 5;
    return n ? g->seed % n : g->seed;
}

void game_new_prompt(struct game *g, int side) {
    struct game_prompt *p = &g->prompt[side];
    const char *keys = side == GAME_LEFT ? LEFT_KEYS : RIGHT_KEYS;
    size_t n = strlen(keys);

    memset(p, 0, sizeof(*p));
    p->half = (uint8_t)side;

    if (g->rung <= RUNG_HAND) {
        snprintf(p->label, sizeof(p->label), "%s", side == GAME_LEFT ? "<ANY" : "ANY>");
        return;
    }

    p->ch = keys[game_rand(g, (uint32_t)n)];

    if (g->rung >= RUNG_CHORD) {
        /* Modifier from the other hand, so the chord is cross-handed. */
        const uint8_t *bits = side == GAME_LEFT ? RIGHT_MOD_BITS : LEFT_MOD_BITS;
        uint8_t pool = g->mod_pool;
        uint8_t allowed[4];
        int n = 0;

        for (int i = 0; i < 4; i++) {
            if (!pool || (pool & bits[i])) {
                allowed[n++] = bits[i];
            }
        }
        p->mods = allowed[game_rand(g, (uint32_t)n)];
        snprintf(p->label, sizeof(p->label), "%c%c", mod_glyph(p->mods), p->ch);
        return;
    }

    snprintf(p->label, sizeof(p->label), "%c", p->ch);
}

static bool prompt_match(const struct game *g, const struct game_prompt *p,
                         const struct game_input *in) {
    if (g->rung <= RUNG_HAND) {
        return in->half == p->half && (in->ch || in->code);
    }
    return in->ch && in->ch == p->ch && in->mods == p->mods;
}

void game_start(struct game *g, const struct game_def *def, uint8_t rung, uint32_t now,
                uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->def = def;
    g->rung = rung ? rung : RUNG_LETTER;
    g->now = now;
    g->started = now;
    g->last_act = now;
    g->seed = seed ? seed : 0x1234567u;

    game_new_prompt(g, GAME_LEFT);
    game_new_prompt(g, GAME_RIGHT);

    if (def->start) {
        def->start(g);
    }
}

void game_key(struct game *g, const struct game_input *in) {
    g->now = in->at;

    /* Any key answers "still playing?" — that is the whole point of the
       question, so it must not also count as a move. */
    if (g->asking) {
        g->asking = false;
        g->last_act = in->at;
        return;
    }

    g->last_act = in->at;

    if (g->over) {
        /* A short deadzone so the keystroke that killed you doesn't also
           restart the run before you've read the score. */
        if (in->at - g->started > 800) {
            uint32_t seed = g->seed;
            uint8_t rung = g->rung;
            const struct game_def *def = g->def;
            uint16_t best = g->best_streak;

            game_start(g, def, rung, in->at, seed);
            g->best_streak = best;
        }
        return;
    }

    for (int side = 0; side < 2; side++) {
        if (!prompt_match(g, &g->prompt[side], in)) {
            continue;
        }

        g->hits++;
        g->streak++;
        if (g->streak > g->best_streak) {
            g->best_streak = g->streak;
        }
        /* The streak bonus is capped: a long run should be worth chasing, not
           worth farming on the easiest rung until the number is meaningless. */
        {
            uint32_t bonus = g->streak / 5;

            g->score += 1 + (bonus > 5 ? 5 : bonus);
        }

        if (g->def->act) {
            g->def->act(g, side == GAME_LEFT ? -1 : 1);
        }
        game_new_prompt(g, side);
        return;
    }

    g->misses++;
    g->streak = 0;
    if (g->def->miss) {
        g->def->miss(g);
    }
}

void game_tick(struct game *g, uint32_t now) {
    uint32_t dt = now - g->now;

    g->now = now;

    /* The world freezes while the question is up: being asked whether you are
       still there must not be able to kill you. */
    if (!g->over && !g->asking && g->def->tick) {
        g->def->tick(g, dt);
    }

    /* Auto-exit. Play keeps you in; quiet gets you asked; silence after the
       question drops you back to the normal screen without you doing anything.
       So the keyboard can never be left silently stuck as a controller. */
    if (!g->asking && now - g->last_act > GAME_IDLE_ASK_MS) {
        g->asking = true;
        g->asked_at = now;
    }
    if (g->asking && now - g->asked_at > GAME_ASK_GRACE_MS) {
        g->want_exit = true;
    }
}

static void draw_panel(struct game_fb *f, int x, int y, int w, int h) {
    fb_rect(f, x, y, w, h, 0);
    fb_frame(f, x, y, w, h, 1);
}

static void draw_hud(struct game *g, struct game_fb *f) {
    char buf[16];
    int top = GAME_PLAY_H + 2;

    fb_hline(f, 0, GAME_PLAY_H, GAME_W, 1);

    fb_text(f, 1, top, g->prompt[GAME_LEFT].label, 1, 1);

    {
        int w = fb_text_w(g->prompt[GAME_RIGHT].label, 1);

        fb_text(f, GAME_W - 1 - w, top, g->prompt[GAME_RIGHT].label, 1, 1);
    }

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)g->score);
    {
        int w = fb_text_w(buf, 1);

        fb_text(f, (GAME_W - w) / 2, top, buf, 1, 1);
    }

    /* The streak is the thing worth chasing, so it gets its own mark rather
       than a number to read: one pip per four in a row, up to the width. */
    if (g->streak >= 4) {
        int pips = g->streak / 4;

        if (pips > 20) {
            pips = 20;
        }
        for (int i = 0; i < pips; i++) {
            fb_px(f, (GAME_W / 2) - pips + i * 2, top + GAME_FONT_H + 1, 1);
        }
    }
}

void game_draw(struct game *g, struct game_fb *f) {
    fb_clear(f);

    if (g->def->draw) {
        g->def->draw(g, f);
    }

    draw_hud(g, f);

    if (g->over) {
        char buf[20];

        draw_panel(f, 14, 14, 100, 34);
        fb_text(f, 20, 18, "GAME OVER", 1, 1);
        snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)g->score);
        fb_text(f, 20, 28, buf, 1, 1);
        snprintf(buf, sizeof(buf), "BEST RUN %u", g->best_streak);
        fb_text(f, 20, 38, buf, 1, 1);
    }

    if (g->asking) {
        uint32_t left = GAME_ASK_GRACE_MS - (g->now - g->asked_at);
        char buf[20];

        draw_panel(f, 10, 18, 108, 28);
        fb_text(f, 16, 23, "STILL PLAYING?", 1, 1);
        snprintf(buf, sizeof(buf), "ANY KEY - %lus", (unsigned long)(left / 1000 + 1));
        fb_text(f, 16, 34, buf, 1, 1);
    }
}

const struct game_def *const game_list[] = {&game_drift, &game_catch, &game_balance};
const int game_count = sizeof(game_list) / sizeof(game_list[0]);
