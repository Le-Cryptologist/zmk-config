/*
 * How the dongle screen says what the keyboard just sent.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk/keys.h>

/* Left and right modifiers say the same thing on screen, so they are folded
   into four bits in the order they are read out: Ctrl, Opt, Shift, Cmd. */
#define TOTEM_MOD_CTRL BIT(0)
#define TOTEM_MOD_OPT BIT(1)
#define TOTEM_MOD_SHIFT BIT(2)
#define TOTEM_MOD_CMD BIT(3)
#define TOTEM_MOD_HYPER (TOTEM_MOD_CTRL | TOTEM_MOD_SHIFT | TOTEM_MOD_CMD)

/* Things the keyboard does that never reach the host as a keycode of their own.
   Dictation is two taps of Ctrl, so only the dongle can tell it happened. */
#define TOTEM_PAGE_GESTURE 0xF000
#define TOTEM_GESTURE_DICTATE 0x01

struct totem_key_desc {
    char sym[28];  /* what goes in the chip on the line, e.g. "⌘C" */
    char name[24]; /* what goes on the bottom line, e.g. "Copy" */
};

uint8_t totem_mods_normalise(zmk_mod_flags_t mods);

/* Fills in both renderings of one key press. */
void totem_key_describe(uint8_t mods, uint16_t page, uint32_t usage, struct totem_key_desc *out);

/* True when this press is just a character being typed, in which case it joins
   the run of text on the line instead of getting a chip of its own. */
bool totem_key_char(uint8_t mods, uint16_t page, uint32_t usage, char *out);

/* Held modifiers, named: "Cmd", "Opt·Shift·Cmd", "Hyper·Opt". */
void totem_mods_caption(uint8_t mods, char *out, size_t out_sz);

/* The same, as symbols for the chip at the edge of the line: "⌥⇧⌘", or "✦⌥"
   when the three that make Hyper are down together. */
void totem_mods_glyphs(uint8_t mods, char *out, size_t out_sz);
