#include "ui_nav.h"
#include "defines.h"
#include "settings.h"
#include "app_state.h"
#include "mqtt_config.h"
#include "sdcard.h"
#include "screen_sd_browser.h"
#include "screen_playlist.h"
#include "ui_manager.h"

// --------------------------------------------------------------------------
// Visibility conditions — a ring entry is shown only if its condition (if any)
// returns true. NULL means "always visible".
// --------------------------------------------------------------------------

static bool cond_sd_screen(void)
{
    if (!app_state_get()->sd_show_screen) return false;
#if defined(BOARD_S3_SUPERMINI)
    // Card is mounted lazily. Hiding SD until then meant the encoder could
    // never open it, so a newly wired reader looked dead. Show the screen;
    // it probes the slot on entry and says NO SD CARD if the mount fails.
    return true;
#else
    return sdcard_is_mounted();
#endif
}
static bool cond_radio_screen(void) { return app_state_get()->radio_show_screen; }
static bool cond_mqtt_enabled(void) { return mqtt_config_get()->enabled; }
#if !defined(BOARD_S3_SUPERMINI)
static bool cond_bt_screen(void)    { return app_state_get()->bt_show_screen; }
#endif

// --------------------------------------------------------------------------
// The navigation map. Edit this table to add/remove/reorder home screens.
// Order defines the swipe / encoder-press cycle; it wraps around.
// --------------------------------------------------------------------------

typedef bool (*nav_cond_fn)(void);

typedef struct {
    ui_screen_id_t id;
    nav_cond_fn    visible;   // NULL = always visible
} nav_ring_entry_t;

static const nav_ring_entry_t s_ring[] = {
#if !defined(BOARD_S3_SUPERMINI)
    { SCREEN_HOME,  NULL              },   // hub / clock
    { SCREEN_BT,    cond_bt_screen    },
#endif
    { SCREEN_RADIO, cond_radio_screen },
#if defined(BOARD_S3_SUPERMINI)
    // Hold walks Radio → playlist → SD folders → Radio. Home is a clock
    // overlay that does not fit 128×64 in this loop (station + title stack).
    { SCREEN_PLAYLIST, cond_radio_screen },
#endif
    { SCREEN_MQTT,  cond_mqtt_enabled },
    { SCREEN_SD,    cond_sd_screen    },
};

#define RING_LEN ((int)(sizeof(s_ring) / sizeof(s_ring[0])))

// --------------------------------------------------------------------------

static int ring_index(ui_screen_id_t id)
{
    for (int i = 0; i < RING_LEN; ++i)
        if (s_ring[i].id == id) return i;
    return -1;
}

static bool ring_visible(int i)
{
    return !s_ring[i].visible || s_ring[i].visible();
}

// Walk `dir` (+1/-1) from `from`, skipping hidden entries, and navigate to the
// first visible one. Bounded by RING_LEN so an all-hidden ring can't loop.
static void ring_step(ui_screen_id_t from, int dir)
{
    int i = ring_index(from);
    if (i < 0) return;

    for (int n = 0; n < RING_LEN; ++n) {
        i = (i + dir + RING_LEN) % RING_LEN;
        if (ring_visible(i)) {
            ui_screen_id_t dest = s_ring[i].id;
#if defined(BOARD_S3_SUPERMINI)
            if (dest == SCREEN_PLAYLIST) {
                screen_playlist_set_return(SCREEN_RADIO);
                ui_navigate(SCREEN_PLAYLIST);
                return;
            }
#endif
            settings_set_screen(dest);
#if defined(BOARD_S3_SUPERMINI)
            // Landing on SD opens the folder list (SD content), not now-playing.
            if (dest == SCREEN_SD && sdcard_is_mounted()) {
                screen_sd_browser_set_return(SCREEN_SD);
                ui_navigate(SCREEN_SD_BROWSER);
            }
#endif
            return;
        }
    }
}

void ui_nav_ring_next(ui_screen_id_t from) { ring_step(from, +1); }
void ui_nav_ring_prev(ui_screen_id_t from) { ring_step(from, -1); }

bool ui_nav_is_ring(ui_screen_id_t id)
{
    int i = ring_index(id);
    return i >= 0 && ring_visible(i);
}
