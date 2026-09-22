/*
 * The Totem dongle game — portable core.
 *
 * Nothing in here knows about ZMK, Zephyr or LVGL. It draws into a plain
 * 1-bit framebuffer laid out exactly like the dongle's canvas (128x64, 16
 * bytes a row, leftmost pixel in the high bit), which is what lets the same
 * code run on the Mac under sim/ and on the dongle under the firmware glue.
 *
 * Tune the game in the simulator. Only flash once it is fun.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef TOTEM_GAME_H
#define TOTEM_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define GAME_W 128
#define GAME_H 64
#define GAME_STRIDE (GAME_W / 8)
#define GAME_BYTES (GAME_STRIDE * GAME_H)

/* The bottom band belongs to the engine: the two prompts and the score. Games
   get everything above it. */
#define GAME_HUD_H 11
#define GAME_PLAY_H (GAME_H - GAME_HUD_H)

/* ============================================================ framebuffer */

struct game_fb {
    uint8_t bits[GAME_BYTES];
};

/* v: 0 = clear, 1 = set, 2 = XOR. Same convention as the status screen. */
void fb_clear(struct game_fb *f);
void fb_px(struct game_fb *f, int x, int y, int v);
int fb_get(const struct game_fb *f, int x, int y);
void fb_rect(struct game_fb *f, int x, int y, int w, int h, int v);
void fb_frame(struct game_fb *f, int x, int y, int w, int h, int v);
void fb_hline(struct game_fb *f, int x, int y, int w, int v);
void fb_vline(struct game_fb *f, int x, int y, int h, int v);

/* Rows are MSB-first, one byte per row for widths up to 8. */
void fb_blit(struct game_fb *f, const uint8_t *rows, int w, int h, int x, int y, int v);

#define GAME_FONT_W 5
#define GAME_FONT_H 7
#define GAME_FONT_FIRST 32
#define GAME_FONT_COUNT 95

extern const uint8_t game_font5x7[GAME_FONT_COUNT][GAME_FONT_H];

int fb_text(struct game_fb *f, int x, int y, const char *s, int scale, int v);
int fb_text_w(const char *s, int scale);

/* =================================================================== input */

enum {
    GAME_LEFT = 0,
    GAME_RIGHT = 1,
};

/* Modifier bits, matching the status screen's normalised set. */
enum {
    GAME_MOD_CTRL = 1 << 0,
    GAME_MOD_ALT = 1 << 1,
    GAME_MOD_SHIFT = 1 << 2,
    GAME_MOD_GUI = 1 << 3,
};

struct game_input {
    uint32_t at;   /* ms */
    uint8_t half;  /* which half of the keyboard it came from */
    char ch;       /* uppercase character, or 0 for a non-character key */
    uint8_t mods;  /* modifiers held when it landed */
    uint16_t code; /* opaque id for non-character keys */
};

/* Which hand a character lives on in the Totem's Colemak-DH base layer.
   Returns GAME_LEFT, GAME_RIGHT, or -1 if it isn't a base-layer character. */
int game_char_half(char c);

/* ============================================================== difficulty */

/* The ladder the whole design turns on: what decides which key you must press.
   Rung 1 trains nothing but alternation; rung 4 trains what you forget. */
enum game_rung {
    RUNG_HAND = 1,   /* any key on that side */
    RUNG_LETTER = 2, /* a named letter on that side */
    RUNG_CHORD = 3,  /* a letter plus a real home-row modifier */
    RUNG_COMBO = 4,  /* a combo or layer key (firmware only for now) */
};

struct game_prompt {
    char label[10]; /* what gets drawn */
    char ch;        /* the character that satisfies it */
    uint8_t mods;   /* modifiers it needs */
    uint8_t half;
};

/* ================================================================== engine */

#define GAME_SCENE_BYTES 320

struct game;

struct game_def {
    const char *name;
    void (*start)(struct game *g);
    void (*act)(struct game *g, int dir);  /* a prompt was satisfied */
    void (*miss)(struct game *g);          /* a key that satisfied nothing */
    void (*tick)(struct game *g, uint32_t dt);
    void (*draw)(struct game *g, struct game_fb *f);
    /* Which way a perfect player would move right now (-1, 0, +1). Optional;
       used by the simulator's autoplay to measure pacing, and available later
       for an attract mode on the dongle. */
    int (*hint)(struct game *g);
};

struct game {
    const struct game_def *def;
    uint32_t now, started, last_act;
    uint32_t score;
    uint16_t streak, best_streak;
    uint16_t hits, misses;
    uint8_t rung;
    /* Which modifiers chord prompts may use, as GAME_MOD_* bits. 0 means all
       four; the simulator narrows it to the ones a terminal can actually
       deliver. */
    uint8_t mod_pool;
    bool over;

    /* Auto-exit. Playing keeps you in; going quiet gets you asked; silence
       after that drops you back to the normal screen on its own. */
    bool asking;
    uint32_t asked_at;
    bool want_exit;

    struct game_prompt prompt[2]; /* [GAME_LEFT] and [GAME_RIGHT] */

    uint32_t seed;
    uint8_t scene[GAME_SCENE_BYTES];
};

#define GAME_IDLE_ASK_MS 60000
#define GAME_ASK_GRACE_MS 10000

void game_start(struct game *g, const struct game_def *def, uint8_t rung, uint32_t now,
                uint32_t seed);
void game_key(struct game *g, const struct game_input *in);
void game_tick(struct game *g, uint32_t now);
void game_draw(struct game *g, struct game_fb *f);

static inline bool game_wants_exit(const struct game *g) { return g->want_exit; }

uint32_t game_rand(struct game *g, uint32_t n);
void game_new_prompt(struct game *g, int side);

/* The games themselves, and a registry so a picker can offer them. */
extern const struct game_def game_drift;
extern const struct game_def game_catch;
extern const struct game_def game_balance;
extern const struct game_def *const game_list[];
extern const int game_count;

#endif /* TOTEM_GAME_H */
