# Key-press recorder

Streams what the keyboard does to a second USB serial port on the dongle, one
line per event, so typing can be analysed on the host. Off at boot; toggled by
the `&rec_toggle` behavior (the Z+X combo on this keymap) and silent unless a program on the
host has the port open.

| Line | Meaning |
| --- | --- |
| `H` | header: firmware build, clock |
| `R` | recording switched on or off |
| `P` | a physical key went down or up, by row/column |
| `K` | a HID keycode was emitted, with its modifiers |
| `L` | the active layer changed |
| `D` | dropped lines, when the host could not keep up |

Everything is timestamped in milliseconds since boot, so hold times, rollover
and combo resolution are all recoverable. `totem-recorder.py` in the project
folder collects it and tags the frontmost app.

Nothing is sent anywhere. The port is a plain CDC ACM device; the host side is
a file on your own machine.

See [`src/recorder.c`](src/recorder.c) for the exact line format.
