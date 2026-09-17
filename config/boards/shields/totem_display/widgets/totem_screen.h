/*
 * The Totem dongle screen.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_totem_screen {
    sys_snode_t node;
    lv_obj_t *obj;
};

int zmk_widget_totem_screen_init(struct zmk_widget_totem_screen *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_totem_screen_obj(struct zmk_widget_totem_screen *widget);
