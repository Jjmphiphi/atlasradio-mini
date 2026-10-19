// Public glue for the auto-update component.
//
// 4 MB Super Mini: dual OTA is disabled (one factory app slot). Pull-OTA,
// GitHub checks, and in-place www refresh are no-ops. Firmware is updated
// over USB with build-flash.py. app_fw_variant() stays real for /api/diag.
#include "updater.h"

#ifndef FW_VARIANT
#define FW_VARIANT "unknown"
#endif

const char *app_fw_variant(void)
{
    return FW_VARIANT;
}

void updater_start(const char *fw_variant, const char *www_expected)
{
    (void)fw_variant;
    (void)www_expected;
}

void updater_set_notify_cb(updater_notify_cb_t cb) { (void)cb; }

void updater_set_progress_cb(updater_progress_cb_t cb) { (void)cb; }

const char *updater_latest_version(void) { return ""; }

bool updater_update_available(void) { return false; }

void updater_apply(void) {}

bool updater_www_stale(const char *expected, char *ver_out, size_t ver_sz)
{
    (void)expected;
    if (ver_out && ver_sz) ver_out[0] = '\0';
    return false;
}

bool updater_www_outdated(void) { return false; }

bool updater_force(updater_force_prep_cb_t prep)
{
    if (prep) prep(UPDATER_FORCE_NONE);
    return true;
}

void updater_www_apply(void) {}
