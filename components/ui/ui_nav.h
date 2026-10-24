#pragma once

#include "ui_events.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --------------------------------------------------------------------------
// Home-screen navigation ring
// --------------------------------------------------------------------------
//
// The set of "home" screens the user cycles through. On the Super Mini a long
// press walks the ring (click picks: station, folder, track). On the larger
// boards a short press / swipe still walks it. Order and visibility live in
// ui_nav.c (s_ring). Hidden entries are skipped.
//
// Super Mini ring is Radio → Playlist → SD (Home is not in it: the 128×64
// clock overlay stacked station + title). Other sub-screens stay out of it.

/** Navigate to the next visible ring screen after `from` (persists via
 *  settings_set_screen). No-op if `from` is not a ring member. Wraps around. */
void ui_nav_ring_next(ui_screen_id_t from);

/** Navigate to the previous visible ring screen before `from`. Wraps around. */
void ui_nav_ring_prev(ui_screen_id_t from);

/** True if `id` is a ring member and currently visible. */
bool ui_nav_is_ring(ui_screen_id_t id);

#ifdef __cplusplus
}
#endif
