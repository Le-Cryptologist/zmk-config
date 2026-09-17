/*
 * Recording indicator for the Totem dongle display.
 *
 * Blank when the recorder is off. "REC" when it is on and the Mac collector is
 * taking the lines. "REC?" when it is on but nothing on the Mac has the port
 * open, i.e. key presses are currently going nowhere.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/recorder_state_changed.h>
#include <zmk/recorder.h>

#include "recorder_status.h"

struct recorder_status_state {
    bool recording;
    bool listening;
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static void recorder_status_update_cb(struct recorder_status_state state) {
    const char *text = !state.recording ? "" : state.listening ? "REC" : "REC?";
    struct zmk_widget_recorder_status *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->obj, text);
    }
}

static struct recorder_status_state recorder_status_get_state(const zmk_event_t *eh) {
    return (struct recorder_status_state){
        .recording = zmk_recorder_is_recording(),
        .listening = zmk_recorder_is_listening(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_recorder_status, struct recorder_status_state,
                            recorder_status_update_cb, recorder_status_get_state)

ZMK_SUBSCRIPTION(widget_recorder_status, zmk_recorder_state_changed);

int zmk_widget_recorder_status_init(struct zmk_widget_recorder_status *widget, lv_obj_t *parent) {
    widget->obj = lv_label_create(parent);
    lv_label_set_text(widget->obj, "");

    sys_slist_append(&widgets, &widget->node);

    widget_recorder_status_init();
    return 0;
}

lv_obj_t *zmk_widget_recorder_status_obj(struct zmk_widget_recorder_status *widget) {
    return widget->obj;
}
