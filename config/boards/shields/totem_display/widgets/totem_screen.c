/*
 * The Totem dongle screen.
 *
 * One picture, drawn by hand into a 128x64 one-bit buffer, rather than a stack
 * of LVGL labels. Everything on this screen needs pixel-level control that
 * labels cannot give: characters are drawn at their own ink width so long names
 * still fit, the line of key presses dissolves into a dither at both ends, the
 * battery percentages are inverted over their own gauges, and presses slide in
 * with a short overshoot. Doing that with widgets would be a fight; a buffer
 * and a handful of drawing helpers is smaller and says exactly what it means.
 *
 * The layout, top to bottom:
 *
 *   row 0    battery percentage in each corner, in a box that drains toward
 *            that corner like a health bar; recorder dot and layer badge
 *            centred between them
 *   rows 12+ the line: what the keyboard has been sending, newest on the right,
 *            shortcuts in chips, typed characters as text, held modifiers as an
 *            inverted chip at the right edge
 *   row 47   what the newest thing on the line is called, in words
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/keys.h>

#if IS_ENABLED(CONFIG_ZMK_RECORDER)
#include <zmk/events/recorder_state_changed.h>
#include <zmk/recorder.h>
#endif

#include "key_names.h"
#include "totem_screen.h"
#include "totem_symbols.h"

#define SCREEN_W 128
#define SCREEN_H 64
#define STRIDE (SCREEN_W / 8)

/* Layout */
#define LINE_TOP 14     /* text top of the line of key presses */
#define BAND_Y0 12      /* rows the line may use; also where it is faded */
#define BAND_Y1 41
#define FADE 10         /* px the line fades over at each end */
#define CAPTION_TOP 47
#define BAT_W 37
#define CENTRE_X0 36    /* the gap between the two battery boxes */
#define CENTRE_X1 92
#define CAPTION_MAX_W 120

/* Motion, all ease-out and all over before you can look away from it */
#define SLIDE_MS 180
#define BOUNCE_MS 240
#define LAYER_MS 180
#define MODS_MS 200
#define TYPE_MS 16      /* per character of the bottom line */
#define BLINK_MS 500
#define ANIM_TAIL_MS 420 /* how long to keep redrawing after something happens */

/* ============================================================ framebuffer */

struct fb {
    uint8_t *bits;
    int16_t y0, y1; /* the rows this buffer holds, in screen coordinates */
    int16_t cx0, cy0, cx1, cy1;
};

/* v: 0 clears, 1 sets, 2 inverts whatever is already there. */
static void fb_px(struct fb *f, int x, int y, int v) {
    if (x < f->cx0 || y < f->cy0 || x >= f->cx1 || y >= f->cy1) {
        return;
    }
    if (x < 0 || x >= SCREEN_W || y < f->y0 || y >= f->y1) {
        return;
    }

    uint8_t *p = &f->bits[(y - f->y0) * STRIDE + (x >> 3)];
    uint8_t mask = 0x80 >> (x & 7);

    if (v == 2) {
        *p ^= mask;
    } else if (v) {
        *p |= mask;
    } else {
        *p &= ~mask;
    }
}

static bool fb_get(const struct fb *f, int x, int y) {
    if (x < 0 || x >= SCREEN_W || y < f->y0 || y >= f->y1) {
        return false;
    }
    return f->bits[(y - f->y0) * STRIDE + (x >> 3)] & (0x80 >> (x & 7));
}

static void fb_fill(struct fb *f, int x, int y, int w, int h, int v) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            fb_px(f, x + i, y + j, v);
        }
    }
}

/* A filled box with the corners knocked off. */
static void fb_pill(struct fb *f, int x, int y, int w, int h, int v) {
    fb_fill(f, x, y, w, h, v);
    fb_px(f, x, y, !v);
    fb_px(f, x + w - 1, y, !v);
    fb_px(f, x, y + h - 1, !v);
    fb_px(f, x + w - 1, y + h - 1, !v);
}

static void fb_frame(struct fb *f, int x, int y, int w, int h) {
    fb_fill(f, x + 1, y, w - 2, 1, 1);
    fb_fill(f, x + 1, y + h - 1, w - 2, 1, 1);
    fb_fill(f, x, y + 1, 1, h - 2, 1);
    fb_fill(f, x + w - 1, y + 1, 1, h - 2, 1);
}

static void fb_bitmap(struct fb *f, const uint16_t *rows, int w, int h, int x, int y, int v) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (rows[j] & (0x8000 >> i)) {
                fb_px(f, x + i, y + j, v);
            }
        }
    }
}

static void fb_clip(struct fb *f, int x0, int y0, int x1, int y1) {
    f->cx0 = x0;
    f->cy0 = y0;
    f->cx1 = x1;
    f->cy1 = y1;
}

static void fb_unclip(struct fb *f) { fb_clip(f, 0, 0, SCREEN_W, SCREEN_H); }

/* ================================================================== text */

struct face {
    const lv_font_t *font;
    int16_t cap;  /* height of a capital letter */
    int16_t base; /* baseline, measured down from the top of the line */
};

static struct face face_big;   /* montserrat 20: the line of key presses */
static struct face face_small; /* unscii 8: everything else */

static void face_init(struct face *face, const lv_font_t *font) {
    lv_font_glyph_dsc_t dsc;

    face->font = font;
    if (lv_font_get_glyph_dsc(font, &dsc, 'H', 0)) {
        face->cap = dsc.box_h;
        face->base = font->line_height - font->base_line - dsc.ofs_y;
    } else {
        face->cap = font->line_height;
        face->base = font->line_height - font->base_line;
    }
}

struct label_style {
    const struct face *face;
    int8_t ls;      /* extra space between characters of the font */
    int8_t gap;     /* space either side of a pixel symbol */
    bool condensed; /* draw each glyph at its own ink width, 1 px apart */
};

static uint32_t utf8_next(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t cp = *p;
    int extra = 0;

    if (cp >= 0xF0) {
        cp &= 0x07;
        extra = 3;
    } else if (cp >= 0xE0) {
        cp &= 0x0F;
        extra = 2;
    } else if (cp >= 0xC0) {
        cp &= 0x1F;
        extra = 1;
    }

    p++;
    for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++, p++) {
        cp = (cp << 6) | (*p & 0x3F);
    }

    *s = (const char *)p;
    return cp;
}

static int utf8_len(const char *s) {
    int n = 0;

    while (*s) {
        utf8_next(&s);
        n++;
    }
    return n;
}

static bool glyph_bit(const uint8_t *bm, int bpp, int p) {
    switch (bpp) {
    case 1:
        return bm[p >> 3] & (0x80 >> (p & 7));
    case 2:
        return ((bm[p >> 2] >> (6 - 2 * (p & 3))) & 0x3) >= 2;
    case 4:
        return ((bm[p >> 1] >> ((p & 1) ? 0 : 4)) & 0x0F) >= 8;
    default:
        return bm[p] >= 128;
    }
}

static void draw_glyph(struct fb *f, const struct face *face, uint32_t cp, int x, int top, int v,
                       bool ink_flush) {
    lv_font_glyph_dsc_t dsc;
    const uint8_t *bm;

    if (!lv_font_get_glyph_dsc(face->font, &dsc, cp, 0)) {
        return;
    }
    bm = lv_font_get_glyph_bitmap(face->font, cp);
    if (!bm) {
        return;
    }

    int gx = ink_flush ? x : x + dsc.ofs_x;
    int gy = top + (face->font->line_height - face->font->base_line) - dsc.box_h - dsc.ofs_y;

    for (int p = 0; p < dsc.box_w * dsc.box_h; p++) {
        if (glyph_bit(bm, dsc.bpp, p)) {
            fb_px(f, gx + (p % dsc.box_w), gy + (p / dsc.box_w), v);
        }
    }
}

/*
 * Draws (or, with f == NULL, just measures) a label: a UTF-8 string in which
 * any character listed in totem_symbols is drawn as pixel art instead of from
 * the font. Only the first max_chars characters are considered, which is how
 * the bottom line types itself out one letter at a time.
 */
static int label_run(struct fb *f, const char *s, int x, int top, int v,
                     const struct label_style *st, int max_chars) {
    const struct face *face = st->face;
    int start = x;
    int prev_kind = -1; /* 0 = from the font, 1 = pixel art */
    int drawn = 0;

    while (*s && drawn < max_chars) {
        uint32_t cp = utf8_next(&s);
        const struct totem_symbol *sym = totem_symbol_find(cp);
        int kind = sym ? 1 : 0;

        if (prev_kind >= 0) {
            x += st->condensed ? 1 : (kind == 0 && prev_kind == 0) ? st->ls : st->gap;
        }

        if (sym) {
            if (f) {
                fb_bitmap(f, sym->rows, sym->w, sym->h, x,
                          top + face->base - (face->cap + 1) / 2 - (sym->h + 1) / 2, v);
            }
            x += sym->w;
        } else {
            lv_font_glyph_dsc_t dsc;
            uint32_t next = 0;

            if (!st->condensed) {
                const char *peek = s;

                if (*peek) {
                    uint32_t n = utf8_next(&peek);

                    if (!totem_symbol_find(n)) {
                        next = n; /* kerning only applies within a run of text */
                    }
                }
            }

            if (lv_font_get_glyph_dsc(face->font, &dsc, cp, next)) {
                if (f) {
                    draw_glyph(f, face, cp, x, top, v, st->condensed);
                }
                /* Condensed: the glyph's own ink width, so "Shift" is narrow
                   where the letters are narrow. A space has no ink at all. */
                x += st->condensed ? (cp == ' ' ? 3 : dsc.box_w) : dsc.adv_w;
            }
        }

        prev_kind = kind;
        drawn++;
    }

    return x - start;
}

static int label_w(const char *s, const struct label_style *st, int max_chars) {
    return label_run(NULL, s, 0, 0, 1, st, max_chars);
}

/* The styles actually used on this screen. */
static struct label_style style_chip;    /* shortcut chips, 20 px */
static struct label_style style_text;    /* typed characters, 20 px */
static struct label_style style_badge;   /* battery and layer badge, 8 px */
static struct label_style style_caption; /* the bottom line, 8 px condensed */

static void styles_init(void) {
    style_chip = (struct label_style){.face = &face_big, .ls = 0, .gap = 1};
    style_text = (struct label_style){.face = &face_big, .ls = 0, .gap = 1};
    style_badge = (struct label_style){.face = &face_small, .ls = 0, .gap = 1};
    style_caption = (struct label_style){.face = &face_small, .ls = 1, .gap = 1, .condensed = true};
}

/* ================================================================ easing */

/* Everything returns 0..1024, LVGL's fixed-point idea of "how far along". */
static int32_t ease_out(uint32_t el, uint32_t dur) {
    if (el >= dur) {
        return 1024;
    }

    int32_t t = 1024 - (int32_t)(el * 1024 / dur); /* 1 - x */
    int64_t q = ((int64_t)t * t) >> 10;

    q = (q * t) >> 10;
    q = (q * t) >> 10;
    return 1024 - (int32_t)q; /* 1 - (1 - x)^4 */
}

/* Goes a little past its destination and settles back: the same curve LVGL
   uses for LV_ANIM_PATH_OVERSHOOT. */
static int32_t overshoot(uint32_t el, uint32_t dur) {
    if (el >= dur) {
        return 1024;
    }
    return (int32_t)lv_bezier3(el * 1024 / dur, 0, 1000, 1300, 1024);
}

/* ================================================================== state */

#define MAX_ENTRIES 12
#define TEXT_MAX 24
#define GAP 4               /* space between two things on the line */
#define BAT_UNKNOWN 0xFF
#define DICTATE_TAP_MS 400  /* two Ctrl taps inside this are a dictation */

struct entry {
    bool text;     /* a run of typed characters rather than a chip */
    char sym[28];  /* the characters, or the chip's glyphs */
    char name[24]; /* what to call it on the bottom line */
    uint8_t repeat;
    uint32_t bump; /* when this entry last grew, for the bounce */
};

static struct {
    struct entry ent[MAX_ENTRIES];
    uint8_t count;
    uint32_t slide_t0;

    uint8_t mods; /* modifiers held right now */
    uint32_t mods_t0;

    char layer[16];
    uint32_t layer_t0;

    uint8_t bat[2]; /* [0] left, [1] right, BAT_UNKNOWN until it reports */

    char caption[40];
    uint32_t caption_t0;

    bool recording, listening;
    uint32_t ctrl_down, ctrl_tap; /* for catching the dictation double tap */
    bool ctrl_solo;

    uint32_t anim_until; /* keep redrawing until this */
    bool blink;
} view;

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static lv_obj_t *canvas;
static uint8_t canvas_buf[LV_CANVAS_BUF_SIZE_INDEXED_1BIT(SCREEN_W, SCREEN_H)];
static uint8_t line_bits[STRIDE * (BAND_Y1 - BAND_Y0)];

/* ================================================================ pieces */

/* The battery gauges lean away from the centre of the screen, which reads as
   two fuel tanks rather than one split bar. */
static int bat_slant(int row) { return (3 * (11 - 1 - row)) / (11 - 1); }

static void battery_box(struct fb *f, uint8_t level, bool right) {
    const int h = 11;
    const int x = right ? SCREEN_W - BAT_W : 0;
    char txt[8];

    for (int j = 0; j < h; j++) {
        int inset = bat_slant(j);
        int x0 = right ? x + inset : x;
        int x1 = right ? x + BAT_W - 1 : x + BAT_W - 1 - inset;

        if (j == 0 || j == h - 1) {
            fb_fill(f, x0, j, x1 - x0 + 1, 1, 1);
        } else {
            fb_px(f, x0, j, 1);
            fb_px(f, x1, j, 1);
        }
    }

    if (level != BAT_UNKNOWN) {
        /* Drains toward its own corner, like a health bar. */
        int fw = ((BAT_W - 4) * level + 50) / 100;

        for (int j = 2; j < h - 2; j++) {
            int inset = bat_slant(j);
            int lo = right ? x + inset + 2 : x + 2;
            int hi = right ? x + BAT_W - 3 : x + BAT_W - 3 - inset;
            int a = right ? hi - fw + 1 : lo;
            int b = right ? hi : lo + fw - 1;

            a = MAX(a, lo);
            b = MIN(b, hi);
            if (b >= a) {
                fb_fill(f, a, j, b - a + 1, 1, 1);
            }
        }
    }

    if (level == BAT_UNKNOWN) {
        strcpy(txt, "--");
    } else {
        snprintf(txt, sizeof(txt), "%u%%", level);
    }

    /* Drawn as an inversion so the number reads against the fill and against
       the empty part of the gauge alike. */
    label_run(f, txt, x + (BAT_W - label_w(txt, &style_badge, 99)) / 2, 1, 2, &style_badge, 99);
}

static const uint16_t rec_dot[5] = {0x7000, 0xF800, 0xF800, 0xF800, 0x7000};
static const uint16_t rec_ring[5] = {0x7000, 0x8800, 0x8800, 0x8800, 0x7000};

static int layer_pill(struct fb *f, int x, int y, int max) {
    const struct label_style *st = &style_badge;
    int w = label_w(view.layer, st, 99) + 4;

    /* A long name gets narrow letters rather than a badge that crowds the
       batteries. */
    if (w > max) {
        st = &style_caption;
        w = label_w(view.layer, st, 99) + 4;
    }

    if (f) {
        fb_pill(f, x, y, w, 11, 1);
        label_run(f, view.layer, x + 2, y + 1, 0, st, 99);
    }
    return w;
}

static void centre_group(struct fb *f, int drop, bool show_dot) {
    int max = CENTRE_X1 - CENTRE_X0 - (show_dot ? 8 : 0);
    int w = layer_pill(NULL, 0, 0, max) + (show_dot ? 5 + GAP : 0);
    int x = CENTRE_X0 + (CENTRE_X1 - CENTRE_X0 - w) / 2;

    if (show_dot) {
        fb_bitmap(f, view.listening ? rec_dot : rec_ring, 5, 5, x, 3 + drop, 1);
        x += 5 + GAP;
    }
    layer_pill(f, x, drop, max);
}

/* The modifiers being held, as an inverted chip sitting at the right edge of
   the line: the press hasn't happened yet, so it isn't part of the stream. */
static int pending_pill(struct fb *f, uint32_t now) {
    char glyphs[24];
    int cap = face_big.cap;
    int y = LINE_TOP + face_big.base - cap - 4;
    int h = cap + 9;

    if (!view.mods) {
        return SCREEN_W;
    }

    totem_mods_glyphs(view.mods, glyphs, sizeof(glyphs));

    int w = label_w(glyphs, &style_chip, 99) + 8;
    int x = SCREEN_W - w;
    uint32_t el = now - view.mods_t0;

    if (el < MODS_MS) {
        x += (w + 2) * (1024 - overshoot(el, MODS_MS)) / 1024;
    }

    fb_pill(f, x, y, w, h, 1);
    label_run(f, glyphs, x + 4, LINE_TOP, 0, &style_chip, 99);
    return x - GAP;
}

static int chip_width(const struct entry *e) {
    int w = label_w(e->sym, &style_chip, 99) + 8;

    if (e->repeat > 1) {
        char rep[8];

        snprintf(rep, sizeof(rep), "×%u", e->repeat);
        w += 2 + label_w(rep, &style_badge, 99);
    }
    return w;
}

static void chip_draw(struct fb *f, const struct entry *e, int x, int top) {
    int cap = face_big.cap;
    int y = top + face_big.base - cap - 4;
    int h = cap + 9;
    int w = chip_width(e);

    fb_frame(f, x, y, w, h);
    fb_px(f, x, y, 0);
    fb_px(f, x + w - 1, y, 0);
    fb_px(f, x, y + h - 1, 0);
    fb_px(f, x + w - 1, y + h - 1, 0);
    label_run(f, e->sym, x + 4, top, 1, &style_chip, 99);

    if (e->repeat > 1) {
        char rep[8];

        snprintf(rep, sizeof(rep), "×%u", e->repeat);
        label_run(f, rep, x + w - 2 - label_w(rep, &style_badge, 99),
                  top + face_big.base - face_small.base, 1, &style_badge, 99);
    }
}

/* Width of the newest thing on the line: how far it has to travel in from the
   right when it appears. */
static int newest_width(void) {
    const struct entry *e;

    if (!view.count) {
        return 0;
    }

    e = &view.ent[view.count - 1];
    if (!e->text) {
        return chip_width(e);
    }

    const char *s = e->sym, *last = e->sym;

    while (*s) {
        last = s;
        utf8_next(&s);
    }
    return label_run(NULL, last, 0, 0, 1, &style_text, 1);
}

/*
 * The line itself, newest on the right, laid out right to left until it runs
 * off the left edge. Characters that only half fit are still drawn: the fade
 * takes care of them, and a glyph dissolving at the edge reads far better than
 * one that simply isn't there.
 */
static void tracker(struct fb *f, int right, uint32_t now) {
    int x = right;
    uint32_t el = now - view.slide_t0;

    if (el < SLIDE_MS) {
        x += (newest_width() + GAP) * (1024 - ease_out(el, SLIDE_MS)) / 1024;
    }

    for (int i = view.count - 1; i >= 0 && x > 0; i--) {
        const struct entry *e = &view.ent[i];
        int bounce = 0;
        uint32_t bel = now - e->bump;

        if (bel < BOUNCE_MS) {
            bounce = -5 + (5 * overshoot(bel, BOUNCE_MS)) / 1024;
        }

        if (!e->text) {
            x -= chip_width(e);
            chip_draw(f, e, x, LINE_TOP + bounce);
        } else {
            /* Walk the run backwards, one character at a time. */
            const char *starts[TEXT_MAX];
            int n = 0;

            for (const char *s = e->sym; *s && n < TEXT_MAX;) {
                starts[n++] = s;
                utf8_next(&s);
            }

            for (int c = n - 1; c >= 0 && x > 0; c--) {
                int cw = label_run(NULL, starts[c], 0, 0, 1, &style_text, 1);

                x -= cw;
                label_run(f, starts[c], x, LINE_TOP + (c == n - 1 ? bounce : 0), 1, &style_text, 1);
            }
        }
        x -= GAP;
    }
}

static const uint8_t bayer[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

/* Copies the line onto the screen, thinning it out over the last few pixels at
   each end so the stream fades into and out of existence. */
static void blit_fade(struct fb *dst, const struct fb *src, int x0, int x1) {
    for (int y = src->y0; y < src->y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (!fb_get(src, x, y)) {
                continue;
            }

            int d = MIN(x - x0, x1 - 1 - x);
            int level = ((d + 1) * 16) / FADE;

            if (level >= 16 || bayer[y & 3][x & 3] < level) {
                fb_px(dst, x, y, 1);
            }
        }
    }
}

/* The bottom line: the newest thing, in words, typed out one letter at a time
   so a glance catches that it changed. */
static void caption(struct fb *f, uint32_t now) {
    int n = utf8_len(view.caption);
    uint32_t el = now - view.caption_t0;
    int shown = el < (uint32_t)n * TYPE_MS ? (int)(el / TYPE_MS) : n;

    while (n > 1 && label_w(view.caption, &style_caption, n) > CAPTION_MAX_W) {
        n--;
    }
    shown = MIN(shown, n);

    int x = (SCREEN_W - label_w(view.caption, &style_caption, n)) / 2;
    int pen = label_run(f, view.caption, x, CAPTION_TOP, 1, &style_caption, shown);

    if (shown < n) {
        fb_fill(f, x + pen + 2, CAPTION_TOP + 1, 6, 7, 1);
    }
}

/* ================================================================= render */

static void render(void) {
    struct fb f = {.bits = canvas_buf + 8, .y0 = 0, .y1 = SCREEN_H};
    struct fb line = {.bits = line_bits, .y0 = BAND_Y0, .y1 = BAND_Y1};
    uint32_t now = k_uptime_get_32();
    uint32_t el;
    int drop = 0, edge;

    if (!canvas) {
        return;
    }

    fb_unclip(&f);
    fb_unclip(&line);
    memset(f.bits, 0, STRIDE * SCREEN_H);
    memset(line.bits, 0, sizeof(line_bits));

    view.blink = (now / BLINK_MS) % 2 == 0;

    battery_box(&f, view.bat[0], false);
    battery_box(&f, view.bat[1], true);

    el = now - view.layer_t0;
    if (el < LAYER_MS) {
        drop = -(11 * (1024 - ease_out(el, LAYER_MS))) / 1024;
    }
    centre_group(&f, drop, view.recording && (view.listening || view.blink));

    edge = pending_pill(&f, now);
    fb_clip(&line, 0, BAND_Y0, edge, BAND_Y1);
    tracker(&line, edge - FADE, now);
    blit_fade(&f, &line, 0, edge);

    caption(&f, now);

    lv_obj_invalidate(canvas);
}

static void tick_cb(lv_timer_t *timer) {
    uint32_t now = k_uptime_get_32();
    bool blink = (now / BLINK_MS) % 2 == 0;

    if (now < view.anim_until || blink != view.blink) {
        render();
    }
}

static void touch(uint32_t now) { view.anim_until = now + ANIM_TAIL_MS; }

static void set_caption(const char *text, uint32_t now) {
    if (strcmp(view.caption, text) == 0) {
        return;
    }
    snprintf(view.caption, sizeof(view.caption), "%s", text);
    view.caption_t0 = now;
}

/* ================================================================== model */

static struct entry *push_entry(uint32_t now) {
    struct entry *e;

    if (view.count == MAX_ENTRIES) {
        memmove(&view.ent[0], &view.ent[1], sizeof(view.ent[0]) * (MAX_ENTRIES - 1));
        view.count--;
    }

    e = &view.ent[view.count++];
    memset(e, 0, sizeof(*e));
    e->repeat = 1;
    e->bump = now;
    view.slide_t0 = now;
    return e;
}

static void add_char(char c, uint32_t now) {
    struct entry *e = view.count ? &view.ent[view.count - 1] : NULL;

    if (!e || !e->text || strlen(e->sym) >= TEXT_MAX - 1) {
        e = push_entry(now);
        e->text = true;
    } else {
        e->bump = now;
        view.slide_t0 = now;
    }

    size_t at = strlen(e->sym);

    e->sym[at] = c;
    e->sym[at + 1] = '\0';
    snprintf(e->name, sizeof(e->name), "%s", c == ' ' ? "Space" : "Typing");
    set_caption(e->name, now);
}

static void add_key(uint8_t mods, uint16_t page, uint32_t usage, uint32_t now) {
    struct totem_key_desc desc;
    struct entry *e = view.count ? &view.ent[view.count - 1] : NULL;

    totem_key_describe(mods, page, usage, &desc);

    /* The same shortcut again just counts up rather than filling the line. */
    if (e && !e->text && strcmp(e->sym, desc.sym) == 0 && e->repeat < 99) {
        e->repeat++;
        e->bump = now;
        view.slide_t0 = now;
    } else {
        e = push_entry(now);
        snprintf(e->sym, sizeof(e->sym), "%s", desc.sym);
    }

    snprintf(e->name, sizeof(e->name), "%s", desc.name);
    set_caption(desc.name, now);
}

static void mods_changed(uint32_t now) {
    char words[32];

    view.mods_t0 = now;
    totem_mods_caption(view.mods, words, sizeof(words));
    if (view.mods) {
        set_caption(words, now);
    } else if (view.count) {
        set_caption(view.ent[view.count - 1].name, now);
    } else {
        set_caption("", now);
    }
}

static void handle_key(const struct zmk_keycode_state_changed *ev) {
    uint32_t now = k_uptime_get_32();
    uint16_t page = ev->usage_page;
    uint32_t usage = ev->keycode;
    uint8_t mods;
    char c;

    touch(now);

    if (page == HID_USAGE_KEY && is_mod(page, usage)) {
        uint8_t bit = totem_mods_normalise(BIT(usage - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL));

        if (ev->state) {
            if (bit == TOTEM_MOD_CTRL) {
                view.ctrl_down = now;
                /* Hyper is Ctrl+Shift+Cmd, and its three presses arrive one at a
                   time, so a tap only counts while Ctrl really is alone. */
                view.ctrl_solo = view.mods == 0;
            } else {
                view.ctrl_solo = false;
            }
            view.mods |= bit;
        } else {
            /* Two quick taps of Ctrl start dictation. Nothing about that reaches
               the host, so the dongle is the only thing that can say it happened. */
            if (bit == TOTEM_MOD_CTRL && view.ctrl_solo && now - view.ctrl_down < 250) {
                if (now - view.ctrl_tap < DICTATE_TAP_MS) {
                    add_key(0, TOTEM_PAGE_GESTURE, TOTEM_GESTURE_DICTATE, now);
                    view.ctrl_tap = 0;
                } else {
                    view.ctrl_tap = now;
                }
            }
            view.mods &= ~bit;
        }
        mods_changed(now);
        render();
        return;
    }

    if (!ev->state) {
        return;
    }

    mods = view.mods | totem_mods_normalise(ev->implicit_modifiers | ev->explicit_modifiers);
    view.ctrl_solo = false;
    view.ctrl_tap = 0;

    if (totem_key_char(mods, page, usage, &c)) {
        add_char(c, now);
    } else {
        add_key(mods, page, usage, now);
    }
    render();
}

/* ================================================================ events */

struct screen_event {
    uint8_t kind; /* 0 = nothing, 1 = key, 2 = layer, 3 = battery, 4 = recorder */
    struct zmk_keycode_state_changed key;
    uint8_t source, level;
    bool recording, listening;
};

static void screen_update_cb(struct screen_event ev) {
    uint32_t now = k_uptime_get_32();

    switch (ev.kind) {
    case 1:
        handle_key(&ev.key);
        return;
    case 2: {
        zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
        const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));
        char fallback[8];

        if (!name || !name[0]) {
            snprintf(fallback, sizeof(fallback), "L%u", (unsigned)index);
            name = fallback;
        }
        if (strcmp(view.layer, name)) {
            snprintf(view.layer, sizeof(view.layer), "%s", name);
            view.layer_t0 = now;
            touch(now);
        }
        break;
    }
    case 3:
        if (ev.source < 2) {
            view.bat[ev.source] = ev.level;
        }
        break;
    case 4:
        view.recording = ev.recording;
        view.listening = ev.listening;
        touch(now);
        break;
    default:
        return;
    }

    render();
}

static struct screen_event screen_get_state(const zmk_event_t *eh) {
    struct screen_event out = {0};
    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    const struct zmk_peripheral_battery_state_changed *bat =
        as_zmk_peripheral_battery_state_changed(eh);

    if (kc) {
        out.kind = 1;
        out.key = *kc;
        return out;
    }
    if (bat) {
        uint8_t source = bat->source;

        out.kind = 3;
#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_SWAP_BATTERIES)
        source = source ? 0 : 1;
#endif
        out.source = source;
        out.level = bat->state_of_charge;
        return out;
    }
#if IS_ENABLED(CONFIG_ZMK_RECORDER)
    if (as_zmk_recorder_state_changed(eh)) {
        out.kind = 4;
        out.recording = zmk_recorder_is_recording();
        out.listening = zmk_recorder_is_listening();
        return out;
    }
#endif
    out.kind = 2; /* the only listener left is the layer one */
    return out;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_totem_screen, struct screen_event, screen_update_cb,
                            screen_get_state)

ZMK_SUBSCRIPTION(widget_totem_screen, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(widget_totem_screen, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(widget_totem_screen, zmk_peripheral_battery_state_changed);
#if IS_ENABLED(CONFIG_ZMK_RECORDER)
ZMK_SUBSCRIPTION(widget_totem_screen, zmk_recorder_state_changed);
#endif

/* ================================================================== setup */

LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_unscii_8);

int zmk_widget_totem_screen_init(struct zmk_widget_totem_screen *widget, lv_obj_t *parent) {
    face_init(&face_big, &lv_font_montserrat_20);
    face_init(&face_small, &lv_font_unscii_8);
    styles_init();

    view.bat[0] = view.bat[1] = BAT_UNKNOWN;
    snprintf(view.layer, sizeof(view.layer), "%s", "Base");

    widget->obj = lv_canvas_create(parent);
    canvas = widget->obj;
    lv_canvas_set_buffer(canvas, canvas_buf, SCREEN_W, SCREEN_H, LV_IMG_CF_INDEXED_1BIT);
    lv_canvas_set_palette(canvas, 0, lv_color_black());
    lv_canvas_set_palette(canvas, 1, lv_color_white());
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);

    render();

    lv_timer_create(tick_cb, 33, NULL);

    sys_slist_append(&widgets, &widget->node);
    widget_totem_screen_init();

    return 0;
}

lv_obj_t *zmk_widget_totem_screen_obj(struct zmk_widget_totem_screen *widget) {
    return widget->obj;
}
