/*
 * The framebuffer. Same layout and same v = 0/1/2 convention as the status
 * screen's, deliberately: whatever gets drawn here can be dropped straight
 * into the dongle's canvas buffer without a conversion step.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "totem_game.h"

void fb_clear(struct game_fb *f) { memset(f->bits, 0, GAME_BYTES); }

void fb_px(struct game_fb *f, int x, int y, int v) {
    if (x < 0 || x >= GAME_W || y < 0 || y >= GAME_H) {
        return;
    }

    uint8_t *byte = &f->bits[y * GAME_STRIDE + (x >> 3)];
    uint8_t mask = 0x80 >> (x & 7);

    switch (v) {
    case 0:
        *byte &= (uint8_t)~mask;
        break;
    case 1:
        *byte |= mask;
        break;
    default:
        *byte ^= mask;
        break;
    }
}

int fb_get(const struct game_fb *f, int x, int y) {
    if (x < 0 || x >= GAME_W || y < 0 || y >= GAME_H) {
        return 0;
    }
    return (f->bits[y * GAME_STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1;
}

void fb_rect(struct game_fb *f, int x, int y, int w, int h, int v) {
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            fb_px(f, col, row, v);
        }
    }
}

void fb_hline(struct game_fb *f, int x, int y, int w, int v) { fb_rect(f, x, y, w, 1, v); }

void fb_vline(struct game_fb *f, int x, int y, int h, int v) { fb_rect(f, x, y, 1, h, v); }

void fb_frame(struct game_fb *f, int x, int y, int w, int h, int v) {
    if (w <= 0 || h <= 0) {
        return;
    }
    fb_hline(f, x, y, w, v);
    fb_hline(f, x, y + h - 1, w, v);
    fb_vline(f, x, y, h, v);
    fb_vline(f, x + w - 1, y, h, v);
}

void fb_blit(struct game_fb *f, const uint8_t *rows, int w, int h, int x, int y, int v) {
    for (int row = 0; row < h; row++) {
        uint8_t bits = rows[row];

        for (int col = 0; col < w; col++) {
            if (bits & (0x80 >> col)) {
                fb_px(f, x + col, y + row, v);
            }
        }
    }
}

/* Text is condensed the same way the status screen does it: glyphs are drawn
   at their natural width and separated by a single column, so a 5-wide font
   reads tighter than a 6-wide grid would. */
static int glyph_index(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c < GAME_FONT_FIRST || c >= GAME_FONT_FIRST + GAME_FONT_COUNT) {
        return -1;
    }
    return c - GAME_FONT_FIRST;
}

int fb_text_w(const char *s, int scale) {
    int w = 0;

    for (; *s; s++) {
        w += (GAME_FONT_W + 1) * scale;
    }
    return w ? w - scale : 0;
}

int fb_text(struct game_fb *f, int x, int y, const char *s, int scale, int v) {
    int at = x;

    for (; *s; s++) {
        int idx = glyph_index(*s);

        if (idx >= 0) {
            const uint8_t *rows = game_font5x7[idx];

            for (int row = 0; row < GAME_FONT_H; row++) {
                for (int col = 0; col < GAME_FONT_W; col++) {
                    if (!(rows[row] & (0x80 >> col))) {
                        continue;
                    }
                    if (scale == 1) {
                        fb_px(f, at + col, y + row, v);
                    } else {
                        fb_rect(f, at + col * scale, y + row * scale, scale, scale, v);
                    }
                }
            }
        }
        at += (GAME_FONT_W + 1) * scale;
    }

    return at - scale - x;
}
