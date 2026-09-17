/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_recorder_state_changed {
    bool recording;
    bool listening;
};

ZMK_EVENT_DECLARE(zmk_recorder_state_changed);
