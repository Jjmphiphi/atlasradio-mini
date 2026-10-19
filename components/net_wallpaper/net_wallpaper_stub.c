#include "net_wallpaper.h"
#include "net_wallpaper_sched.h"

bool net_wallpaper_fetch(int slot, const char *url, int panel_w, int panel_h)
{
    (void)slot;
    (void)url;
    (void)panel_w;
    (void)panel_h;
    return false;
}

bool net_wallpaper_fetch_all(int panel_w, int panel_h)
{
    (void)panel_w;
    (void)panel_h;
    return false;
}

const char *net_wallpaper_status(void)
{
    return "idle";
}

void net_wallpaper_progress(int *done, int *total)
{
    if (done) *done = 0;
    if (total) *total = 0;
}

const lv_image_dsc_t *net_wallpaper_image(int slot)
{
    (void)slot;
    return NULL;
}

bool net_wallpaper_any_image(void)
{
    return false;
}

void net_wallpaper_commit(void) {}

bool net_wallpaper_save_to_sd(int slot, char *out_path, size_t out_cap, const char **err)
{
    (void)slot;
    (void)out_path;
    (void)out_cap;
    if (err) *err = "unsupported";
    return false;
}

uint8_t *net_wallpaper_bin_snapshot(int slot, size_t *out_len)
{
    (void)slot;
    if (out_len) *out_len = 0;
    return NULL;
}

void net_wallpaper_dismiss(int slot)
{
    (void)slot;
}

void net_wallpaper_set_done_cb(void (*cb)(bool ok))
{
    (void)cb;
}

void net_wallpaper_set_start_cb(void (*cb)(void))
{
    (void)cb;
}

void net_wallpaper_sched_init(int panel_w, int panel_h)
{
    (void)panel_w;
    (void)panel_h;
}

void net_wallpaper_sched_set_boot_done_cb(void (*cb)(void))
{
    (void)cb;
}

bool net_wallpaper_sched_boot_fetch_pending(void)
{
    return false;
}

void net_wallpaper_sched_update(void) {}

void net_wallpaper_sched_fetch_done(bool ok)
{
    (void)ok;
}
