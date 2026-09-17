/*
 * Key output log widget for the Totem dongle display.
 *
 * Shows what the keyboard actually SENT, not what you pressed. That distinction
 * is the whole point: a combo, a home-row mod, a tap-dance and a plain key all
 * look the same under your fingers, and only the emitted HID report tells you
 * which one the firmware decided on. Tap the backspace combo and you either see
 * "bspc" or you see "u y" — which answers the question immediately, without
 * opening an event viewer on the Mac.
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
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

#include "keylog.h"

/* The unscii-8 font is 8 px wide with a 1 px letter gap, so fourteen glyphs is
   as much as fits across a 128 px panel. Two rows of that is roughly the last
   half-dozen things the keyboard said. */
#define KEYLOG_COLS 14
#define KEYLOG_ROWS 2
#define KEYLOG_HISTORY 10

/* Longest key name, e.g. "prtsc". A modifier prefix of up to "G-C-A-S-" can sit
   in front of it, and the two together are exactly one row wide. */
#define KEYLOG_NAME_MAX 6
#define KEYLOG_ENTRY_MAX (8 + KEYLOG_NAME_MAX)

struct keylog_state {
    char text[KEYLOG_ROWS * (KEYLOG_COLS + 1) + 1];
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static char history[KEYLOG_HISTORY][KEYLOG_ENTRY_MAX + 1];
static uint8_t history_len;

/* Modifiers arrive as their own key events rather than as flags on the key they
   modify, so hold-shift-then-press only reads correctly if we track them. */
static zmk_mod_flags_t held_mods;

struct named_key {
    uint16_t id;
    const char *name;
};

static const struct named_key named_keys[] = {
    {HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, "ret"},
    {HID_USAGE_KEY_KEYBOARD_ESCAPE, "esc"},
    {HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, "bspc"},
    {HID_USAGE_KEY_KEYBOARD_TAB, "tab"},
    {HID_USAGE_KEY_KEYBOARD_SPACEBAR, "spc"},
    {HID_USAGE_KEY_KEYBOARD_CAPS_LOCK, "caps"},
    {HID_USAGE_KEY_KEYBOARD_PRINTSCREEN, "prtsc"},
    {HID_USAGE_KEY_KEYBOARD_INSERT, "ins"},
    {HID_USAGE_KEY_KEYBOARD_HOME, "home"},
    {HID_USAGE_KEY_KEYBOARD_PAGEUP, "pgup"},
    {HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD, "del"},
    {HID_USAGE_KEY_KEYBOARD_END, "end"},
    {HID_USAGE_KEY_KEYBOARD_PAGEDOWN, "pgdn"},
    {HID_USAGE_KEY_KEYBOARD_RIGHTARROW, "rgt"},
    {HID_USAGE_KEY_KEYBOARD_LEFTARROW, "lft"},
    {HID_USAGE_KEY_KEYBOARD_DOWNARROW, "dn"},
    {HID_USAGE_KEY_KEYBOARD_UPARROW, "up"},
    {HID_USAGE_KEY_KEYPAD_SLASH, "kp/"},
    {HID_USAGE_KEY_KEYPAD_ASTERISK, "kp*"},
    {HID_USAGE_KEY_KEYPAD_MINUS, "kp-"},
    {HID_USAGE_KEY_KEYPAD_PLUS, "kp+"},
    {HID_USAGE_KEY_KEYPAD_ENTER, "kpent"},
    {HID_USAGE_KEY_KEYPAD_PERIOD_AND_DELETE, "kp."},
};

static const struct named_key consumer_keys[] = {
    {HID_USAGE_CONSUMER_PLAY, "play"},
    {HID_USAGE_CONSUMER_PAUSE, "paus"},
    {HID_USAGE_CONSUMER_SCAN_NEXT_TRACK, "next"},
    {HID_USAGE_CONSUMER_SCAN_PREVIOUS_TRACK, "prev"},
    {HID_USAGE_CONSUMER_STOP, "stop"},
    {HID_USAGE_CONSUMER_PLAY_PAUSE, "play"},
    {HID_USAGE_CONSUMER_MUTE, "mute"},
    {HID_USAGE_CONSUMER_VOLUME_INCREMENT, "vol+"},
    {HID_USAGE_CONSUMER_VOLUME_DECREMENT, "vol-"},
    {HID_USAGE_CONSUMER_AC_SEARCH, "srch"},
    {HID_USAGE_CONSUMER_AC_HOME, "www"},
    {HID_USAGE_CONSUMER_AC_BACK, "back"},
    {HID_USAGE_CONSUMER_AC_FORWARD, "fwd"},
};

/* The ten digit keys and the punctuation keys each carry a second glyph that
   Shift selects. Printing that glyph rather than "S-4" is what makes the log
   read like the text you were trying to produce. */
static const char digits_plain[] = "1234567890";
static const char digits_shifted[] = "!@#$%^&*()";

struct punct_key {
    uint16_t id;
    char plain;
    char shifted;
};

static const struct punct_key punct_keys[] = {
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

static const char *lookup(const struct named_key *table, size_t len, uint16_t id) {
    for (size_t i = 0; i < len; i++) {
        if (table[i].id == id) {
            return table[i].name;
        }
    }
    return NULL;
}

/* Fills `base` with the key's own name and reports whether Shift was spent on
   choosing that name (in which case it must not also be printed as a prefix). */
static bool key_base_name(uint16_t page, uint32_t id, bool shift, char *base, size_t base_sz) {
    const char *name;

    if (page == HID_USAGE_CONSUMER) {
        name = lookup(consumer_keys, ARRAY_SIZE(consumer_keys), id);
        if (name) {
            snprintf(base, base_sz, "%s", name);
        } else {
            snprintf(base, base_sz, "c%03X", (unsigned)id);
        }
        return false;
    }

    if (page != HID_USAGE_KEY) {
        snprintf(base, base_sz, "?%03X", (unsigned)id);
        return false;
    }

    if (id >= HID_USAGE_KEY_KEYBOARD_A && id <= HID_USAGE_KEY_KEYBOARD_Z) {
        char c = (char)('a' + (id - HID_USAGE_KEY_KEYBOARD_A));
        snprintf(base, base_sz, "%c", shift ? (char)(c - 'a' + 'A') : c);
        return shift;
    }

    if (id >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
        id <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        size_t i = id - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        snprintf(base, base_sz, "%c", shift ? digits_shifted[i] : digits_plain[i]);
        return shift;
    }

    for (size_t i = 0; i < ARRAY_SIZE(punct_keys); i++) {
        if (punct_keys[i].id == id) {
            snprintf(base, base_sz, "%c", shift ? punct_keys[i].shifted : punct_keys[i].plain);
            return shift;
        }
    }

    if (id >= HID_USAGE_KEY_KEYBOARD_F1 && id <= HID_USAGE_KEY_KEYBOARD_F12) {
        snprintf(base, base_sz, "F%u", (unsigned)(id - HID_USAGE_KEY_KEYBOARD_F1 + 1));
        return false;
    }

    if (id >= HID_USAGE_KEY_KEYBOARD_F13 && id <= HID_USAGE_KEY_KEYBOARD_F24) {
        snprintf(base, base_sz, "F%u", (unsigned)(id - HID_USAGE_KEY_KEYBOARD_F13 + 13));
        return false;
    }

    if (id >= HID_USAGE_KEY_KEYPAD_1_AND_END && id <= HID_USAGE_KEY_KEYPAD_0_AND_INSERT) {
        size_t i = id - HID_USAGE_KEY_KEYPAD_1_AND_END;
        snprintf(base, base_sz, "kp%c", digits_plain[i]);
        return false;
    }

    name = lookup(named_keys, ARRAY_SIZE(named_keys), id);
    if (name) {
        snprintf(base, base_sz, "%s", name);
        return false;
    }

    snprintf(base, base_sz, "%03X", (unsigned)id);
    return false;
}

static void format_key(uint16_t page, uint32_t id, zmk_mod_flags_t mods, char *out,
                       size_t out_sz) {
    char base[KEYLOG_NAME_MAX + 1] = {};
    bool shift = (mods & (MOD_LSFT | MOD_RSFT)) != 0;

    /* If Shift went into choosing the glyph — "$" rather than "S-4" — it must
       not also be printed as a prefix. */
    bool shift_used = key_base_name(page, id, shift, base, sizeof(base));

    char prefix[9] = {};
    size_t p = 0;

    if (mods & (MOD_LGUI | MOD_RGUI)) {
        prefix[p++] = 'G';
        prefix[p++] = '-';
    }
    if (mods & (MOD_LCTL | MOD_RCTL)) {
        prefix[p++] = 'C';
        prefix[p++] = '-';
    }
    if (mods & (MOD_LALT | MOD_RALT)) {
        prefix[p++] = 'A';
        prefix[p++] = '-';
    }
    if (shift && !shift_used) {
        prefix[p++] = 'S';
        prefix[p++] = '-';
    }
    prefix[p] = '\0';

    snprintf(out, out_sz, "%s%s", prefix, base);
}

static void history_push(const char *entry) {
    if (history_len == KEYLOG_HISTORY) {
        memmove(history[0], history[1], (KEYLOG_HISTORY - 1) * sizeof(history[0]));
        history_len--;
    }

    strncpy(history[history_len], entry, KEYLOG_ENTRY_MAX);
    history[history_len][KEYLOG_ENTRY_MAX] = '\0';
    history_len++;
}

/* Packs the history into the rows from the bottom right backwards, so the newest
   entry is always on screen and older ones fall off the top left. */
static void render(char *out, size_t out_sz) {
    char rows[KEYLOG_ROWS][KEYLOG_COLS + 1];

    for (int r = 0; r < KEYLOG_ROWS; r++) {
        memset(rows[r], ' ', KEYLOG_COLS);
        rows[r][KEYLOG_COLS] = '\0';
    }

    int row = KEYLOG_ROWS - 1;
    int pos = KEYLOG_COLS;

    for (int i = (int)history_len - 1; i >= 0 && row >= 0; i--) {
        int len = (int)strlen(history[i]);

        if (len > KEYLOG_COLS) {
            len = KEYLOG_COLS;
        }

        int sep = (pos < KEYLOG_COLS) ? 1 : 0;

        if (pos - sep - len < 0) {
            row--;
            if (row < 0) {
                break;
            }
            pos = KEYLOG_COLS;
            sep = 0;
            if (pos - len < 0) {
                continue;
            }
        }

        pos -= sep + len;
        memcpy(rows[row] + pos, history[i], len);
    }

    out[0] = '\0';

    for (int r = 0; r < KEYLOG_ROWS; r++) {
        const char *start = rows[r];

        while (*start == ' ') {
            start++;
        }

        strncat(out, start, out_sz - strlen(out) - 1);

        if (r < KEYLOG_ROWS - 1) {
            strncat(out, "\n", out_sz - strlen(out) - 1);
        }
    }
}

static void keylog_update_cb(struct keylog_state state) {
    struct zmk_widget_keylog *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->obj, state.text);
    }
}

/* Runs in the event context, which is where the history has to be accumulated:
   the display work item coalesces, so several presses can share a single redraw
   and only the accumulated text would survive. */
static struct keylog_state keylog_get_state(const zmk_event_t *eh) {
    struct keylog_state state = {};
    const struct zmk_keycode_state_changed *ev =
        (eh != NULL) ? as_zmk_keycode_state_changed(eh) : NULL;

    if (ev != NULL) {
        if (is_mod(ev->usage_page, ev->keycode)) {
            zmk_mod_flags_t bit =
                (zmk_mod_flags_t)BIT(ev->keycode - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);

            if (ev->state) {
                held_mods |= bit;
            } else {
                held_mods &= (zmk_mod_flags_t)~bit;
            }
        } else if (ev->state) {
            char entry[KEYLOG_ENTRY_MAX + 1];

            format_key(ev->usage_page, ev->keycode,
                       ev->implicit_modifiers | ev->explicit_modifiers | held_mods, entry,
                       sizeof(entry));
            history_push(entry);
        }
    }

    render(state.text, sizeof(state.text));
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_keylog, struct keylog_state, keylog_update_cb,
                            keylog_get_state)

ZMK_SUBSCRIPTION(widget_keylog, zmk_keycode_state_changed);

int zmk_widget_keylog_init(struct zmk_widget_keylog *widget, lv_obj_t *parent) {
    widget->obj = lv_label_create(parent);
    lv_obj_set_width(widget->obj, 128);
    lv_label_set_long_mode(widget->obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(widget->obj, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(widget->obj, "");

    sys_slist_append(&widgets, &widget->node);

    widget_keylog_init();
    return 0;
}

lv_obj_t *zmk_widget_keylog_obj(struct zmk_widget_keylog *widget) {
    return widget->obj;
}
