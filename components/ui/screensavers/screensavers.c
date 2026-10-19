#include "screensavers.h"
#include "screensaver_dashboard.h"
#include "defines.h"
#include <string.h>

#if defined(UI_PROFILE_MONO_128X64)
void screensaver_dashboard_settings_changed(void) {}
#endif

extern const ui_screen_t screensaver_clockdigital;
extern const ui_screen_t screensaver_mqtt;
extern const ui_screen_t screensaver_blank;
#if !defined(UI_PROFILE_MONO_128X64)
extern const ui_screen_t screensaver_clockhands;
extern const ui_screen_t screensaver_starfield;
extern const ui_screen_t screensaver_fireworks;
extern const ui_screen_t screensaver_plasma;
extern const ui_screen_t screensaver_life;
extern const ui_screen_t screensaver_dashboard;
extern const ui_screen_t screensaver_photo;
#endif

static const struct {
    screensaver_id_t        id;
    const ui_screen_t      *screen;
    const char             *name;
} k_map[] = {
#if defined(UI_PROFILE_MONO_128X64)
    { SCREENSAVER_CLOCKHANDS,   &screensaver_clockdigital, "clockhands"   },
    { SCREENSAVER_CLOCKDIGITAL, &screensaver_clockdigital, "clockdigital" },
    { SCREENSAVER_STARFIELD,    &screensaver_clockdigital, "starfield"    },
    { SCREENSAVER_FIREWORKS,    &screensaver_blank,        "fireworks"    },
    { SCREENSAVER_PLASMA,       &screensaver_blank,        "plasma"       },
    { SCREENSAVER_LIFE,         &screensaver_blank,        "life"         },
    { SCREENSAVER_DASHBOARD,    &screensaver_blank,        "dashboard"    },
    { SCREENSAVER_MQTT,         &screensaver_mqtt,         "mqtt"         },
    { SCREENSAVER_PHOTO,        &screensaver_blank,        "photo"        },
    { SCREENSAVER_BLANK,        &screensaver_blank,        "blank"        },
    { SCREENSAVER_DIM,          &screensaver_blank,        "dim"          },
#else
    { SCREENSAVER_CLOCKHANDS,   &screensaver_clockhands,   "clockhands"   },
    { SCREENSAVER_CLOCKDIGITAL, &screensaver_clockdigital, "clockdigital" },
    { SCREENSAVER_STARFIELD,    &screensaver_starfield,    "starfield"    },
    { SCREENSAVER_FIREWORKS,    &screensaver_fireworks,    "fireworks"    },
    { SCREENSAVER_PLASMA,       &screensaver_plasma,       "plasma"       },
    { SCREENSAVER_LIFE,         &screensaver_life,         "life"         },
    { SCREENSAVER_DASHBOARD,    &screensaver_dashboard,    "dashboard"    },
    { SCREENSAVER_MQTT,         &screensaver_mqtt,         "mqtt"         },
    { SCREENSAVER_PHOTO,        &screensaver_photo,        "photo"        },
    { SCREENSAVER_BLANK,        &screensaver_blank,        "blank"        },
    { SCREENSAVER_DIM,          &screensaver_blank,        "dim"          },
#endif
};

#define K_MAP_LEN (sizeof(k_map) / sizeof(k_map[0]))

const ui_screen_t *screensaver_get(screensaver_id_t id)
{
    for (size_t i = 0; i < K_MAP_LEN; i++) {
        if (k_map[i].id == id) return k_map[i].screen;
    }
#if defined(UI_PROFILE_MONO_128X64)
    return &screensaver_clockdigital;
#else
    return &screensaver_clockhands;
#endif
}

const char *screensaver_name(screensaver_id_t id)
{
    for (size_t i = 0; i < K_MAP_LEN; i++) {
        if (k_map[i].id == id) return k_map[i].name;
    }
#if defined(UI_PROFILE_MONO_128X64)
    return "clockdigital";
#else
    return "clockhands";
#endif
}

screensaver_id_t screensaver_from_name(const char *name)
{
#if defined(UI_PROFILE_MONO_128X64)
    if (!name) return SCREENSAVER_CLOCKDIGITAL;
#else
    if (!name) return SCREENSAVER_CLOCKHANDS;
#endif
    for (size_t i = 0; i < K_MAP_LEN; i++) {
        if (strcmp(k_map[i].name, name) == 0) return k_map[i].id;
    }
#if defined(UI_PROFILE_MONO_128X64)
    return SCREENSAVER_CLOCKDIGITAL;
#else
    return SCREENSAVER_CLOCKHANDS;
#endif
}

bool screensaver_is_valid(int id)
{
    return id >= 0 && id < SCREENSAVER_COUNT;
}
