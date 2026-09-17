# The header is global so the display shield can draw the REC indicator.
zephyr_include_directories(include)

# Sources go straight onto the app target rather than into a library of their
# own. That puts the recorder's event listener ahead of combos and hold-taps in
# the link order, which is what lets it see every raw key press before either of
# them has a chance to swallow or delay it.
if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources_ifdef(CONFIG_ZMK_RECORDER app PRIVATE src/recorder.c)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_RECORDER_TOGGLE app PRIVATE src/behavior_recorder_toggle.c)
endif()
