/*
 * Key output log widget for the Totem dongle display.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_keylog {
    sys_snode_t node;
    lv_obj_t *obj;
};

int zmk_widget_keylog_init(struct zmk_widget_keylog *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_keylog_obj(struct zmk_widget_keylog *widget);
