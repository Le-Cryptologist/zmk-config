/*
 * The browser harness's C side. A handful of flat functions over one static
 * game, so the JavaScript never has to know what a struct looks like. The
 * engine and the games are the same sources the dongle will run.
 *
 * SPDX-License-Identifier: MIT
 */

#include <emscripten/emscripten.h>
#include <string.h>

#include "totem_game.h"

static struct game g;
static struct game_fb fb;

EMSCRIPTEN_KEEPALIVE int web_game_count(void) { return game_count; }

EMSCRIPTEN_KEEPALIVE const char *web_game_name(int i) {
    return i >= 0 && i < game_count ? game_list[i]->name : "";
}

EMSCRIPTEN_KEEPALIVE void web_start(int which, int rung, unsigned now, unsigned seed,
                                    int mod_pool) {
    if (which < 0 || which >= game_count) {
        which = 0;
    }
    game_start(&g, game_list[which], (uint8_t)rung, now, seed);
    g.mod_pool = (uint8_t)mod_pool;
    game_new_prompt(&g, GAME_LEFT);
    game_new_prompt(&g, GAME_RIGHT);
}

EMSCRIPTEN_KEEPALIVE int web_char_half(int ch) { return game_char_half((char)ch); }

EMSCRIPTEN_KEEPALIVE void web_key(unsigned at, int half, int ch, int mods) {
    struct game_input in;

    memset(&in, 0, sizeof(in));
    in.at = at;
    in.half = (uint8_t)half;
    in.ch = (char)ch;
    in.mods = (uint8_t)mods;
    game_key(&g, &in);
}

EMSCRIPTEN_KEEPALIVE int web_tick(unsigned now) {
    game_tick(&g, now);
    return game_wants_exit(&g);
}

EMSCRIPTEN_KEEPALIVE const uint8_t *web_draw(void) {
    game_draw(&g, &fb);
    return fb.bits;
}

/* Stats, by index, so the page can show them without a struct layout. */
EMSCRIPTEN_KEEPALIVE unsigned web_stat(int i) {
    switch (i) {
    case 0:
        return g.score;
    case 1:
        return g.hits;
    case 2:
        return g.misses;
    case 3:
        return g.streak;
    case 4:
        return g.best_streak;
    case 5:
        return g.over;
    case 6:
        return g.asking;
    case 7:
        return g.rung;
    case 8:
        return (unsigned)(g.now - g.started);
    default:
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE const char *web_prompt(int side) {
    return g.prompt[side ? GAME_RIGHT : GAME_LEFT].label;
}
