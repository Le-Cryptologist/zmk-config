/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

/* Recording switched on with the toggle. */
bool zmk_recorder_is_recording(void);

/* A program on the host has the recorder's serial port open. Key events are
   only ever sent when both this and recording are true. */
bool zmk_recorder_is_listening(void);

void zmk_recorder_toggle(void);
