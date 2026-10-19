#pragma once

#include "sdkconfig.h"

// www partition (editable UI) is mounted at /spiffs; user data lives on
// a separate `config` partition mounted at /config.
#define WEB_ROOT "/spiffs"
#define CONFIG_ROOT "/config"
#define SETTINGS_FILE "/config/settings.json"
#define PLAYLIST_FILE "/config/playlist.csv"
#define PLAYLIST_FILE_LEGACY "/spiffs/playlist.csv"
#define THEME_FILE "/config/theme.json"
#define EVENTS_FILE "/config/events.json"

// ===== GPIO MAP (ESP32-S3 Super Mini, easily accessible header = 1–13) =====
// In-package / reserved — never route to a peripheral:
//   0          BOOT strapping
//   19, 20     USB D-/D+ (native USB-CDC console)
//   26–32      SPI flash (in-package)
//   33–37      in-package PSRAM
//   43, 44     UART0
//   45, 46     strapping
// GPIO 3 is JTAG_SEL (high at reset enables JTAG on 39–42, unused here).
// Assigned to the encoder switch (idle pulled high).

// ===== USER CONFIG =====
// CMake reads DISPLAY_ / TOUCH_ / FLASH_ and generates sdkconfig.variant.

#define BOARD_S3_SUPERMINI

// ===== FLASH SIZE =====
#define FLASH_4MB

// ===== DISPLAY DRIVER =====
#define DISPLAY_SSD1306

// ===== UI PROFILE =====
#define UI_PROFILE_MONO_128X64

// ===== TOUCH DRIVER =====
#define TOUCH_NONE

#define DISPLAY_HOST      SPI2_HOST
#define DISPLAY_CLK_SPEED   40000000

#define UI_SHOW_FPS_OVERLAY 0

#if CONFIG_DISPLAY_SSD1306

#define OLED_PIN_SDA  8
#define OLED_PIN_SCL  9
#define OLED_I2C_ADDR 0x3C
#define OLED_PIN_RST  -1

#else
    #error "Unknown DISPLAY_TYPE"
#endif

// ===== TOUCH (unused on this SKU) =====
#define CTP_SCL     -1
#define CTP_SDA     -1
#define CTP_INT     -1
#define CTP_RST     -1

// ===== SD CARD (SPI) =====
#define HAS_SD_CARD
#define HAS_SD_SPI
#define SD_PIN_CLK   -1   // SDMMC unused
#define SD_PIN_CMD   -1
#define SD_PIN_D0    -1
#define SD_PIN_CD    -1
#define SD_PIN_SCK   12
#define SD_PIN_MOSI  11
#define SD_PIN_MISO  13
#define SD_PIN_CS    10

// ===== I2S MAX98357A =====
#define I2S_BCK         1
#define I2S_LCK         2
#define I2S_DATA        4
#define AMP_SD_PIN      7   // mute: drive LOW; unmute: high-Z (470 kΩ to 5 V for L+R mix)

// ===== RADIO =====
#define RADIO_VOL_STEP 3

// ===== BLUETOOTH (no QCC5125 on this SKU) =====
#define BT_MOULE_PIN            -1
#define BT_MODULE_TX_PIN        -1
#define BT_MODULE_RX_PIN        -1
#define BT_UART_NUM             UART_NUM_1
#define BT_UART_BAUD            115200
#define BT_VOL_STEP 5

// ===== ENCODER =====
#define ENC_CLK_PIN         5
#define ENC_DT_PIN          6
#define ENC_BTN_PIN         3

// ===== BUZZER =====
#define BUZZER_PIN     -1

// ===== WIFI =====
#define WIFI_AP_SSID             "AtlasRadio"
#define WIFI_AP_PASS             "99876543"

// ===== NTP =====
#define DEFAULT_NTP_SERVER1  "pool.ntp.org"
#define DEFAULT_NTP_SERVER2  "time.cloudflare.com"
#define DEFAULT_TZ_STRING    "CET-1CEST,M3.5.0/2,M10.5.0/3"
