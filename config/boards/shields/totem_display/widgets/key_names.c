/*
 * How the dongle screen says what the keyboard just sent.
 *
 * The keyboard emits HID usages; a usage on its own ("KEY 0x07 with GUI") is
 * not something you can read at a glance. This file is the agreed wording: one
 * table of shortcuts, each with the short form that goes in the chip on the
 * line and the plain-English name that goes underneath it. Anything not in the
 * table still renders — modifier glyphs followed by the key — so an unnamed
 * shortcut is readable, just not named.
 *
 * Fork note: this table is the one file to edit if your keymap sends different
 * shortcuts. Nothing else on the screen needs to know about them.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zephyr/sys/util.h>

#include "key_names.h"

/* Modifier glyphs and words, in read-out order. */
static const struct {
    uint8_t bit;
    const char *glyph;
    const char *word;
} mod_names[] = {
    {TOTEM_MOD_CTRL, "⌃", "Ctrl"},
    {TOTEM_MOD_OPT, "⌥", "Opt"},
    {TOTEM_MOD_SHIFT, "⇧", "Shift"},
    {TOTEM_MOD_CMD, "⌘", "Cmd"},
};

/* Keys with a name or a symbol of their own. */
static const struct {
    uint16_t page;
    uint16_t usage;
    const char *label; /* in a chip */
    const char *name;  /* spelled out */
} key_labels[] = {
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, "⏎", "Return"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_TAB, "⇥", "Tab"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, "⌫", "Backspace"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD, "⌦", "Fwd delete"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_ESCAPE, "Esc", "Escape"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR, "␣", "Space"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTARROW, "←", "Left"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_RIGHTARROW, "→", "Right"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_UPARROW, "↑", "Up"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DOWNARROW, "↓", "Down"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_PAGEUP, "PgUp", "Page up"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_PAGEDOWN, "PgDn", "Page down"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_HOME, "Home", "Home"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_END, "End", "End"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_INSERT, "Ins", "Insert"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_CAPS_LOCK, "Caps", "Caps lock"},
    {HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_PRINTSCREEN, "PrtSc", "Print screen"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_PLAY_PAUSE, "⏯", "Play/pause"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_PLAY, "⏯", "Play"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_PAUSE, "⏯", "Pause"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_SCAN_NEXT_TRACK, "⏭", "Next track"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_SCAN_PREVIOUS_TRACK, "⏮", "Prev track"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_VOLUME_INCREMENT, "🔊", "Volume up"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_VOLUME_DECREMENT, "🔉", "Volume down"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_MUTE, "🔇", "Mute"},
    {HID_USAGE_CONSUMER, HID_USAGE_CONSUMER_AC_SEARCH, "Srch", "Search"},
    {TOTEM_PAGE_GESTURE, TOTEM_GESTURE_DICTATE, "🎤", "Dictation"},
};

/*
 * The shortcut table: the whole point of the screen. Read it as "when the
 * keyboard sends this, say that". Order does not matter; the first match wins.
 */
static const struct {
    uint8_t mods;
    uint16_t page;
    uint16_t usage;
    const char *sym;
    const char *name;
} shortcuts[] = {
    /* Editing, mostly from the combos on the left hand */
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_Z, "⌘Z", "Undo"},
    {TOTEM_MOD_CMD | TOTEM_MOD_SHIFT, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_Z, "⌘⇧Z", "Redo"},
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_X, "⌘X", "Cut"},
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_C, "⌘C", "Copy"},
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_V, "⌘V", "Paste"},
    {TOTEM_MOD_CMD | TOTEM_MOD_SHIFT, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_V, "⌘⇧V",
     "Clipboard list"},
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_A, "⌘A", "Select all"},
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_S, "⌘S", "Save"},
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F, "⌘F", "Find"},

    /* Moving around: windows, spaces, apps */
    {TOTEM_MOD_CTRL, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTARROW, "⌃←", "Desktop left"},
    {TOTEM_MOD_CTRL, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_RIGHTARROW, "⌃→", "Desktop right"},
    {TOTEM_MOD_CTRL, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_UPARROW, "⌃↑", "Mission Ctrl"},
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, "⌘`",
     "Next window"},
    {TOTEM_MOD_OPT, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_TAB, "⌥⇥", "App switcher"},
    {TOTEM_MOD_CMD, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_TAB, "⌘⇥", "App switcher"},

    /* Screenshots */
    {TOTEM_MOD_CMD | TOTEM_MOD_SHIFT, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_3_AND_HASH, "⌘⇧3",
     "Screenshot"},
    {TOTEM_MOD_CMD | TOTEM_MOD_SHIFT, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, "⌘⇧4",
     "Area shot"},
    {TOTEM_MOD_CMD | TOTEM_MOD_SHIFT, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT, "⌘⇧5",
     "Screenshot app"},
    {TOTEM_MOD_HYPER, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, "✦4", "Area to clipbd"},

    /* Dictation apps */
    {0, HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F17, "F17", "MacWhisper"},
};

uint8_t totem_mods_normalise(zmk_mod_flags_t mods) {
    uint8_t out = 0;

    if (mods & (MOD_LCTL | MOD_RCTL)) {
        out |= TOTEM_MOD_CTRL;
    }
    if (mods & (MOD_LALT | MOD_RALT)) {
        out |= TOTEM_MOD_OPT;
    }
    if (mods & (MOD_LSFT | MOD_RSFT)) {
        out |= TOTEM_MOD_SHIFT;
    }
    if (mods & (MOD_LGUI | MOD_RGUI)) {
        out |= TOTEM_MOD_CMD;
    }
    return out;
}

/* The ten digit keys and the punctuation keys each carry a second character
   that Shift selects; printing that is what makes typed text read as text. */
static const char digits_plain[] = "1234567890";
static const char digits_shifted[] = "!@#$%^&*()";

static const struct {
    uint16_t usage;
    char plain;
    char shifted;
} punct_keys[] = {
    {HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, '-', '_'},
    {HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, '=', '+'},
    {HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE, '[', '{'},
    {HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE, ']', '}'},
    {HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, '\\', '|'},
    {HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON, ';', ':'},
    {HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, '\'', '"'},
    {HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, '`', '~'},
    {HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN, ',', '<'},
    {HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN, '.', '>'},
    {HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK, '/', '?'},
};

bool totem_key_char(uint8_t mods, uint16_t page, uint32_t usage, char *out) {
    bool shift = (mods & TOTEM_MOD_SHIFT) != 0;

    /* Shift is part of typing; any other modifier makes this a shortcut. */
    if (page != HID_USAGE_KEY || (mods & ~TOTEM_MOD_SHIFT)) {
        return false;
    }

    if (usage >= HID_USAGE_KEY_KEYBOARD_A && usage <= HID_USAGE_KEY_KEYBOARD_Z) {
        char c = (char)('a' + (usage - HID_USAGE_KEY_KEYBOARD_A));
        *out = shift ? (char)(c - 'a' + 'A') : c;
        return true;
    }
    if (usage >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
        usage <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        size_t i = usage - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        *out = shift ? digits_shifted[i] : digits_plain[i];
        return true;
    }
    if (usage == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
        *out = ' ';
        return true;
    }
    for (size_t i = 0; i < ARRAY_SIZE(punct_keys); i++) {
        if (punct_keys[i].usage == usage) {
            *out = shift ? punct_keys[i].shifted : punct_keys[i].plain;
            return true;
        }
    }
    return false;
}

/* The key on its own, with no modifiers: "C", "⏎", "F13", "5". */
static void key_label(uint16_t page, uint32_t usage, bool spelled, char *out, size_t out_sz) {
    for (size_t i = 0; i < ARRAY_SIZE(key_labels); i++) {
        if (key_labels[i].page == page && key_labels[i].usage == usage) {
            snprintf(out, out_sz, "%s", spelled ? key_labels[i].name : key_labels[i].label);
            return;
        }
    }

    if (page == HID_USAGE_KEY) {
        if (usage >= HID_USAGE_KEY_KEYBOARD_A && usage <= HID_USAGE_KEY_KEYBOARD_Z) {
            snprintf(out, out_sz, "%c", (char)('A' + (usage - HID_USAGE_KEY_KEYBOARD_A)));
            return;
        }
        if (usage >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
            usage <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
            snprintf(out, out_sz, "%c",
                     digits_plain[usage - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION]);
            return;
        }
        for (size_t i = 0; i < ARRAY_SIZE(punct_keys); i++) {
            if (punct_keys[i].usage == usage) {
                snprintf(out, out_sz, "%c", punct_keys[i].plain);
                return;
            }
        }
        if (usage >= HID_USAGE_KEY_KEYBOARD_F1 && usage <= HID_USAGE_KEY_KEYBOARD_F12) {
            snprintf(out, out_sz, "F%u", (unsigned)(usage - HID_USAGE_KEY_KEYBOARD_F1 + 1));
            return;
        }
        if (usage >= HID_USAGE_KEY_KEYBOARD_F13 && usage <= HID_USAGE_KEY_KEYBOARD_F24) {
            snprintf(out, out_sz, "F%u", (unsigned)(usage - HID_USAGE_KEY_KEYBOARD_F13 + 13));
            return;
        }
    }

    snprintf(out, out_sz, "%03X", (unsigned)usage);
}

void totem_mods_caption(uint8_t mods, char *out, size_t out_sz) {
    size_t used = 0;

    out[0] = '\0';
    if (!mods) {
        return;
    }

    /* Ctrl+Shift+Cmd is the Hyper key on this board, not three modifiers, and
       saying so is both shorter and truer to what the keyboard did. */
    if ((mods & TOTEM_MOD_HYPER) == TOTEM_MOD_HYPER) {
        used = snprintf(out, out_sz, "Hyper");
        if (mods & TOTEM_MOD_OPT) {
            snprintf(out + used, out_sz - used, "·Opt");
        }
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(mod_names); i++) {
        if (!(mods & mod_names[i].bit)) {
            continue;
        }
        used += snprintf(out + used, out_sz - used, "%s%s", used ? "·" : "", mod_names[i].word);
        if (used >= out_sz) {
            return;
        }
    }
}

void totem_mods_glyphs(uint8_t mods, char *out, size_t out_sz) {
    size_t used = 0;

    out[0] = '\0';
    if (!mods) {
        return;
    }

    if ((mods & TOTEM_MOD_HYPER) == TOTEM_MOD_HYPER) {
        used = snprintf(out, out_sz, "✦");
        if (mods & TOTEM_MOD_OPT) {
            snprintf(out + used, out_sz - used, "⌥");
        }
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(mod_names); i++) {
        if (!(mods & mod_names[i].bit)) {
            continue;
        }
        used += snprintf(out + used, out_sz - used, "%s", mod_names[i].glyph);
        if (used >= out_sz) {
            return;
        }
    }
}

void totem_key_describe(uint8_t mods, uint16_t page, uint32_t usage, struct totem_key_desc *out) {
    char label[12];

    for (size_t i = 0; i < ARRAY_SIZE(shortcuts); i++) {
        if (shortcuts[i].mods == mods && shortcuts[i].page == page &&
            shortcuts[i].usage == usage) {
            snprintf(out->sym, sizeof(out->sym), "%s", shortcuts[i].sym);
            snprintf(out->name, sizeof(out->name), "%s", shortcuts[i].name);
            return;
        }
    }

    /* Hyper plus a letter: a whole layer of shortcuts that only this keyboard
       sends, so name it after the key rather than the three modifiers. */
    if (mods == TOTEM_MOD_HYPER) {
        key_label(page, usage, false, label, sizeof(label));
        snprintf(out->sym, sizeof(out->sym), "✦%s", label);
        snprintf(out->name, sizeof(out->name), "Hyper %s", label);
        return;
    }

    key_label(page, usage, false, label, sizeof(label));
    out->sym[0] = '\0';
    for (size_t i = 0; i < ARRAY_SIZE(mod_names); i++) {
        if (mods & mod_names[i].bit) {
            strncat(out->sym, mod_names[i].glyph, sizeof(out->sym) - strlen(out->sym) - 1);
        }
    }
    strncat(out->sym, label, sizeof(out->sym) - strlen(out->sym) - 1);

    key_label(page, usage, true, label, sizeof(label));
    if (mods) {
        char words[24];

        totem_mods_caption(mods, words, sizeof(words));
        snprintf(out->name, sizeof(out->name), "%.11s %.11s", words, label);
    } else {
        snprintf(out->name, sizeof(out->name), "%s", label);
    }
}
