/*
 * The simulator: the dongle's 128x64 screen, in a terminal.
 *
 * This exists because finding out whether a game is fun is iteration-bound,
 * not code-bound. The same engine and game sources compile here natively, so a
 * tuning change is a two-second rebuild instead of a five-minute build and
 * flash. Only the input and the output differ.
 *
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "totem_game.h"

/* A terminal can deliver Ctrl and Shift on a letter. It cannot deliver Gui,
   and Alt is unreliable across terminal emulators, so chord prompts here are
   drawn from the two that survive the trip. */
#define SIM_MOD_POOL (GAME_MOD_CTRL | GAME_MOD_SHIFT)

static struct termios saved_tio;
static bool tio_saved;

static void restore(void) {
    if (tio_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    }
    printf("\x1b[?25h\x1b[0m\n");
    fflush(stdout);
}

static void raw_mode(void) {
    struct termios tio;

    if (tcgetattr(STDIN_FILENO, &saved_tio) != 0) {
        return;
    }
    tio_saved = true;
    atexit(restore);

    tio = saved_tio;
    tio.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG);
    tio.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &tio);

    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
    printf("\x1b[?25l");
}

static uint32_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000));
}

/* Two screen rows per terminal row, via the half-block glyphs. 128x64 lands in
   an 128x32 terminal window, which is a normal size. */
static void present(const struct game_fb *f, const char *status) {
    static char out[64 * 1024];
    size_t n = 0;

    n += (size_t)sprintf(out + n, "\x1b[H");
    for (int y = 0; y < GAME_H; y += 2) {
        for (int x = 0; x < GAME_W; x++) {
            int top = fb_get(f, x, y);
            int bot = fb_get(f, x, y + 1);
            const char *px = top && bot ? "█" : top ? "▀" : bot ? "▄" : " ";

            n += (size_t)sprintf(out + n, "%s", px);
        }
        n += (size_t)sprintf(out + n, "\x1b[K\n");
    }
    n += (size_t)sprintf(out + n, "\x1b[K%s", status);
    (void)!write(STDOUT_FILENO, out, n);
}

/* Decode one byte of terminal input into the same shape the firmware will hand
   the engine: a character, a modifier set, and which hand it came from. */
static bool decode(int c, struct game_input *in) {
    memset(in, 0, sizeof(*in));

    if (c >= 1 && c <= 26 && c != '\t' && c != '\n' && c != '\r') {
        in->mods = GAME_MOD_CTRL;
        in->ch = (char)('A' + c - 1);
    } else if (c >= 'A' && c <= 'Z') {
        in->mods = GAME_MOD_SHIFT;
        in->ch = (char)c;
    } else if (c >= 'a' && c <= 'z') {
        in->ch = (char)(c - 'a' + 'A');
    } else {
        return false;
    }

    {
        int half = game_char_half(in->ch);

        if (half < 0) {
            return false;
        }
        in->half = (uint8_t)half;
    }
    return true;
}

int main(int argc, char **argv) {
    struct game g;
    struct game_fb fb;
    uint8_t rung = RUNG_LETTER;
    int which = 0;
    uint32_t seed = now_ms();
    bool autoplay = false;
    bool quiet = false;
    int auto_frames = 3000;
    int auto_ms = 250;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-g") && i + 1 < argc) {
            which = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-r") && i + 1 < argc) {
            rung = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "-a")) {
            autoplay = true;
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            auto_frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            auto_ms = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-q")) {
            quiet = true;
        } else {
            fprintf(stderr, "usage: %s [-g game] [-r 1..3] [-s seed] [-a [-i ms] [-n frames] [-q]]\n", argv[0]);
            return 2;
        }
    }
    if (rung < RUNG_HAND) {
        rung = RUNG_HAND;
    }
    if (rung > RUNG_CHORD) {
        rung = RUNG_CHORD; /* rung 4 is combos: firmware only */
    }

    if (which < 0 || which >= game_count) {
        which = 0;
    }
    game_start(&g, game_list[which], rung, now_ms(), seed);
    g.mod_pool = SIM_MOD_POOL;
    game_new_prompt(&g, GAME_LEFT);
    game_new_prompt(&g, GAME_RIGHT);

    if (autoplay) {
        /* A headless smoke run: a perfect player who can answer a prompt every
           `auto_ms` milliseconds. Sweeping that interval is how we find out
           what reaction speed each rung really demands. */
        uint32_t t = g.now;
        uint32_t next = g.now;

        for (int i = 0; i < auto_frames && !g.over; i++) {
            t += 33;
            game_tick(&g, t);
            while (next <= t && !g.over) {
                int dir = g.def->hint ? g.def->hint(&g) : 0;

                next += (uint32_t)auto_ms;
                if (!dir) {
                    continue;
                }
                {
                    struct game_input in;
                    int side = dir < 0 ? GAME_LEFT : GAME_RIGHT;

                    memset(&in, 0, sizeof(in));
                    in.at = t;
                    in.half = (uint8_t)side;
                    /* At rung 1 the prompt names no letter, so the bot plays
                       one from the right hand, as a person would. */
                    in.ch = g.prompt[side].ch ? g.prompt[side].ch
                                              : (side == GAME_LEFT ? 'S' : 'E');
                    in.mods = g.prompt[side].mods;
                    game_key(&g, &in);
                }
            }
        }
        if (!quiet) {
            game_draw(&g, &fb);
            for (int y = 0; y < GAME_H; y += 2) {
                for (int x = 0; x < GAME_W; x++) {
                    int top = fb_get(&fb, x, y), bot = fb_get(&fb, x, y + 1);

                    fputs(top && bot ? "\u2588" : top ? "\u2580" : bot ? "\u2584" : " ",
                          stdout);
                }
                fputc('\n', stdout);
            }
        }
        printf("rung=%u every=%dms lived=%.1fs score=%u hits=%u best=%u died=%d\n", g.rung,
               auto_ms, (t - g.started) / 1000.0, g.score, g.hits, g.best_streak, g.over);
        return 0;
    }

    raw_mode();
    printf("\x1b[2J");

    for (;;) {
        char buf[32];
        ssize_t got = read(STDIN_FILENO, buf, sizeof(buf));
        char status[160];

        for (ssize_t i = 0; i < got; i++) {
            struct game_input in;
            int c = (unsigned char)buf[i];

            if (c == 3 || c == 27) { /* Ctrl-C or Esc */
                return 0;
            }
            if (decode(c, &in)) {
                in.at = now_ms();
                game_key(&g, &in);
            }
        }

        game_tick(&g, now_ms());
        if (game_wants_exit(&g)) {
            printf("\nidle - left game mode\n");
            return 0;
        }

        game_draw(&g, &fb);
        snprintf(status, sizeof(status),
                 " %s  rung %u  hits %u  miss %u  best %u   [esc quits]", g.def->name,
                 g.rung, g.hits, g.misses, g.best_streak);
        present(&fb, status);

        {
            struct timespec sl = {.tv_sec = 0, .tv_nsec = 20 * 1000000L};

            nanosleep(&sl, NULL);
        }
    }
}
