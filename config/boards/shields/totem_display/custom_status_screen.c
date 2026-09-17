/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "custom_status_screen.h"
#include "widgets/battery_status.h"
#include "widgets/modifiers.h"
#include "widgets/bongo_cat.h"
#include "widgets/layer_status.h"
#include "widgets/output_status.h"
#include "widgets/hid_indicators.h"
#include "widgets/keylog.h"
#include "widgets/wpm_status.h"
#include "widgets/recorder_status.h"
#include "widgets/totem_screen.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_SCREEN)

/* One canvas that draws the whole screen itself. See widgets/totem_screen.c. */
static struct zmk_widget_totem_screen totem_screen_widget;

#else

static struct zmk_widget_output_status output_status_widget;
static struct zmk_widget_dongle_battery_status dongle_battery_status_widget;

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_LAYER)
static struct zmk_widget_layer_status layer_status_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_MODIFIERS)
static struct zmk_widget_modifiers modifiers_widget;
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
static struct zmk_widget_hid_indicators hid_indicators_widget;
#endif

#endif

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_BONGO_CAT)
static struct zmk_widget_bongo_cat bongo_cat_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_KEYLOG)
static struct zmk_widget_keylog keylog_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_RECORDER)
static struct zmk_widget_recorder_status recorder_status_widget;
#endif

static struct zmk_widget_wpm_status wpm_status_widget;

#endif /* CONFIG_ZMK_TOTEM_DISPLAY_SCREEN */

lv_style_t global_style;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen;

    screen = lv_obj_create(NULL);

    lv_style_init(&global_style);
    lv_style_set_text_font(&global_style, &lv_font_unscii_8);
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_SCREEN)

    zmk_widget_totem_screen_init(&totem_screen_widget, screen);

#else

    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_LEFT, 0, 0);

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_WPM)
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_align_to(zmk_widget_wpm_status_obj(&wpm_status_widget), zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_OUT_RIGHT_MID, 7, 0);
#endif

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_BONGO_CAT)
    zmk_widget_bongo_cat_init(&bongo_cat_widget, screen);
    lv_obj_align(zmk_widget_bongo_cat_obj(&bongo_cat_widget), LV_ALIGN_BOTTOM_RIGHT, 0, -7);
#endif

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_KEYLOG)
    zmk_widget_keylog_init(&keylog_widget, screen);
    lv_obj_align(zmk_widget_keylog_obj(&keylog_widget), LV_ALIGN_TOP_LEFT, 0, 21);
#endif

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_MODIFIERS)
    zmk_widget_modifiers_init(&modifiers_widget, screen);
    lv_obj_align(zmk_widget_modifiers_obj(&modifiers_widget), LV_ALIGN_BOTTOM_LEFT, 0, 0);
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    zmk_widget_hid_indicators_init(&hid_indicators_widget, screen);
    lv_obj_align_to(zmk_widget_hid_indicators_obj(&hid_indicators_widget), zmk_widget_modifiers_obj(&modifiers_widget), LV_ALIGN_OUT_RIGHT_MID, 3, 0);
#endif
#endif

#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_LAYER)
    zmk_widget_layer_status_init(&layer_status_widget, screen);
#if IS_ENABLED(CONFIG_ZMK_TOTEM_DISPLAY_BONGO_CAT)
    lv_obj_align_to(zmk_widget_layer_status_obj(&layer_status_widget), zmk_widget_bongo_cat_obj(&bongo_cat_widget), LV_ALIGN_BOTTOM_RIGHT, 0, 5);
#else
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_BOTTOM_RIGHT, 0, -3);
#endif
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY)
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget), LV_ALIGN_TOP_RIGHT, 0, 0);
#endif

#if IS_ENABLED(CONFIG_ZMK_RECORDER)
    // Top centre: the one gap in the top row, between the output and battery icons.
    zmk_widget_recorder_status_init(&recorder_status_widget, screen);
    lv_obj_align(zmk_widget_recorder_status_obj(&recorder_status_widget), LV_ALIGN_TOP_MID, 0, 0);
#endif

#endif /* CONFIG_ZMK_TOTEM_DISPLAY_SCREEN */

    return screen;
}