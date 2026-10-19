#include "bt_module.h"
#include "defines.h"

#if defined(BT_MODULE_QCC5125V2)

// Feasycom FSC-BT1026E / QCC5125, "v2" AT firmware. Command set verified against
// the module on the AtlasCube board. Differences from the older v1 firmware that
// motivated this abstraction: pause is AT+PU (v1: AT+PJ), volume-up AT+VP
// (v1: AT+VU), audio mode AT+AUDMx (v1: AT+AUDMOD), and v1 has no AT+SVSTEP.
static const bt_module_desc_t s_qcc5125v2 = {
    .name             = "QCC5125v2",

    .cmd_play         = "AT+PA",
    .cmd_pause        = "AT+PU",
    .cmd_next         = "AT+PN",
    .cmd_prev         = "AT+PV",
    .cmd_get_state    = "AT+STATE",
    .cmd_get_meta     = "AT+GMETA",
    .cmd_meta_time_on = "AT+SMTIMEON",
    .cmd_get_codec    = "AT+CODE",
    .cmd_get_arate    = "AT+GARATE",
    .cmd_reboot       = "AT+BOOT",

    .cmd_sync_vol_on    = "AT+SYNCVOLON",
    .cmd_sync_vol_off   = "AT+SYNCVOLOFF",
    .cmd_sync_vol_state = "AT+SYNCVOLSTATE",

    .vol_max       = 127,            // 128 steps (0..127), enforced at init
    .fmt_set_vol   = "AT+SVOL=%d",
    .cmd_set_vstep = "AT+SVSTEP=128",// must match vol_max+1; applies on reboot

    .evt_connected     = "BT_CN",
    .evt_connected_alt = "Connected",
    .evt_disconnected  = "BT_DC",
    .evt_discoverable  = "ConnDiscoverable",
    .evt_play          = "BT_PA",
    .evt_src_none      = "+SRC=NONE",
    .key_title         = "+TITL=",
    .key_artist        = "+ARTS=",
    .key_dur_ms        = "+PYTM=",
    .key_pos_s         = "+PYPS=",
    .key_codec         = "+Code1=",
    .key_arate         = "+ARATE=",
    .key_abit          = "+ABIT=",
};

const bt_module_desc_t *const g_bt = &s_qcc5125v2;

#else

// Super Mini (and any board without BT_MODULE_*): keep a dummy dialect so
// bt.c links. Every send path no-ops when UART pins are unset.
static const bt_module_desc_t s_none = {
    .name             = "none",
    .cmd_play         = "",
    .cmd_pause        = "",
    .cmd_next         = "",
    .cmd_prev         = "",
    .cmd_get_state    = "",
    .cmd_get_meta     = "",
    .cmd_meta_time_on = "",
    .cmd_get_codec    = "",
    .cmd_get_arate    = "",
    .cmd_reboot       = "",
    .cmd_sync_vol_on    = "",
    .cmd_sync_vol_off   = "",
    .cmd_sync_vol_state = "",
    .vol_max       = 100,
    .fmt_set_vol   = "AT+SVOL=%d",
    .cmd_set_vstep = "",
    .evt_connected     = "\x01",
    .evt_connected_alt = 0,
    .evt_disconnected  = "\x02",
    .evt_discoverable  = "\x03",
    .evt_play          = "\x04",
    .evt_src_none      = "\x05",
    .key_title         = "\x06",
    .key_artist        = "\x07",
    .key_dur_ms        = "\x08",
    .key_pos_s         = "",
    .key_codec         = "\x09",
    .key_arate         = "\x0A",
    .key_abit          = "\x0B",
};

const bt_module_desc_t *const g_bt = &s_none;

#endif // BT_MODULE_QCC5125V2
