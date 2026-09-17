/*
 * Key-press recorder.
 *
 * Streams every raw key press and release, every keycode the board sends and
 * every layer change to a USB serial port of its own, one short text line each,
 * so a program on the Mac can keep a timed record of how the keyboard is used.
 * That record is what the hold-tap, combo and idle timings get tuned against.
 *
 * Two gates, both of which have to be open before a key event leaves the board:
 * recording has to be switched on (off at every boot), and a program on the Mac
 * has to have the port open (DTR asserted). Close the collector and the board
 * stops sending immediately, even with recording still on.
 *
 * Line format, all numbers decimal except where noted. Every line starts with
 * the board's uptime in ms when the line was written, then how many ms earlier
 * the event itself happened. For a raw press that gap is ~0; for a keycode it is
 * how long a hold-tap or combo sat on the press before deciding.
 *
 *   H <now> <age> <protocol> <recording>        hello, whenever the port opens
 *   R <now> <age> <recording>                   recording switched on/off
 *   P <now> <age> <position> <pressed> <source> raw key, source = which half
 *   K <now> <age> <page hex> <usage hex> <implicit mods hex> <explicit mods hex> <pressed>
 *   L <now> <age> <layer> <active>
 *   D <now> <age> <bytes>                       bytes dropped since the last line
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdarg.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/recorder_state_changed.h>
#include <zmk/recorder.h>

#define RECORDER_PROTOCOL 1
#define RECORDER_LINE_MAX 64
#define RECORDER_POLL_MS 500

ZMK_EVENT_IMPL(zmk_recorder_state_changed);

static const struct device *const port = DEVICE_DT_GET(DT_CHOSEN(zmk_recorder_uart));

K_MUTEX_DEFINE(recorder_lock);

static bool recording;
static bool listening;
static uint32_t dropped;

static bool port_open(void) {
    uint32_t dtr = 0;

    return uart_line_ctrl_get(port, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr;
}

static void put(const char *buf, int len) {
    int wrote = uart_fifo_fill(port, (const uint8_t *)buf, len);

    if (wrote < len) {
        dropped += len - MAX(wrote, 0);
    }
}

/* Writes one line if a program is listening. A full buffer drops the tail of the
   line rather than blocking the key path; the count goes out ahead of the next
   line that fits, led by a newline so a half-written line can't swallow it. */
static void emit(int64_t timestamp, const char *fmt, ...) {
    char line[RECORDER_LINE_MAX];
    int64_t now = k_uptime_get();
    int len;

    if (k_is_in_isr() || !port_open()) {
        return;
    }

    len = snprintf(line, sizeof(line), "%c %u %u ", fmt[0], (uint32_t)now,
                   (uint32_t)MAX(now - timestamp, 0));

    va_list args;
    va_start(args, fmt);
    len += vsnprintf(line + len, sizeof(line) - len, fmt + 1, args);
    va_end(args);
    len = MIN(len, (int)sizeof(line) - 1);

    k_mutex_lock(&recorder_lock, K_FOREVER);

    if (dropped) {
        char note[32];
        int note_len = snprintf(note, sizeof(note), "\nD %u 0 %u\n", (uint32_t)now, dropped);

        dropped = 0;
        put(note, note_len);
    }
    put(line, len);

    k_mutex_unlock(&recorder_lock);
}

static void notify(void) {
    raise_zmk_recorder_state_changed(
        (struct zmk_recorder_state_changed){.recording = recording, .listening = listening});
}

bool zmk_recorder_is_recording(void) { return recording; }

bool zmk_recorder_is_listening(void) { return listening; }

void zmk_recorder_toggle(void) {
    recording = !recording;
    emit(k_uptime_get(), "R%u\n", recording);
    notify();
}

static int recorder_listener(const zmk_event_t *eh) {
    if (!recording) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        emit(pos->timestamp, "P%u %u %u\n", pos->position, pos->state, pos->source);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_keycode_state_changed *key = as_zmk_keycode_state_changed(eh);
    if (key != NULL) {
        emit(key->timestamp, "K%x %x %x %x %u\n", key->usage_page, key->keycode,
             key->implicit_modifiers, key->explicit_modifiers, key->state);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(eh);
    if (layer != NULL) {
        emit(layer->timestamp, "L%u %u\n", layer->layer, layer->state);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(recorder, recorder_listener);
ZMK_SUBSCRIPTION(recorder, zmk_position_state_changed);
ZMK_SUBSCRIPTION(recorder, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(recorder, zmk_layer_state_changed);

/* There is no callback for the host opening the port, so look twice a second.
   A fresh listener gets a hello line saying whether recording is on, which is
   how the Mac side tells this port apart from the Studio one. */
static void poll_port(struct k_work *work) {
    bool open = port_open();

    if (open != listening) {
        listening = open;
        if (open) {
            emit(k_uptime_get(), "H%u %u\n", RECORDER_PROTOCOL, recording);
        }
        notify();
    }

    k_work_schedule(k_work_delayable_from_work(work), K_MSEC(RECORDER_POLL_MS));
}

static K_WORK_DELAYABLE_DEFINE(poll_work, poll_port);

static int recorder_init(void) {
    if (!device_is_ready(port)) {
        LOG_ERR("Recorder serial port not ready");
        return -ENODEV;
    }

    k_work_schedule(&poll_work, K_MSEC(RECORDER_POLL_MS));
    return 0;
}

SYS_INIT(recorder_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
