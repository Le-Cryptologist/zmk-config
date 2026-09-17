/*
 * Pixel symbols for the Totem dongle screen.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

struct totem_symbol {
    uint32_t codepoint; /* the character that stands for this symbol in a label */
    uint8_t w;
    uint8_t h;
    const uint16_t *rows; /* one bitmap per row, most significant bit leftmost */
};

extern const struct totem_symbol totem_symbols[];
extern const size_t totem_symbol_count;

static inline const struct totem_symbol *totem_symbol_find(uint32_t codepoint) {
    for (size_t i = 0; i < totem_symbol_count; i++) {
        if (totem_symbols[i].codepoint == codepoint) {
            return &totem_symbols[i];
        }
    }
    return NULL;
}
