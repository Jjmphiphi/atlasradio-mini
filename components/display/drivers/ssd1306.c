/*
 * ssd1306.c — SSD1306 128x64 1-bit OLED over I2C (addr 0x3C).
 *
 * LVGL renders RGB565. flush_cb converts to 1-bit and writes only dirty
 * 8-pixel pages. Contrast register 0x81 is display_set_backlight().
 */

#include "lvgl.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "defines.h"
#include "board_pins.h"
#include "ui_profile.h"
#include "settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SSD1306";

#define SSD1306_I2C_HZ      400000
#define SSD1306_PAGES       (DISPLAY_HEIGHT / 8)
#define SSD1306_CTRL_CMD    0x00
#define SSD1306_CTRL_DATA   0x40

static i2c_master_bus_handle_t  s_bus;
static i2c_master_dev_handle_t  s_dev;
static SemaphoreHandle_t        s_i2c_mtx;

static bool          s_flip_on    = false;
static volatile bool s_flip_dirty = false;

#define I2C_LOCK()    xSemaphoreTakeRecursive(s_i2c_mtx, portMAX_DELAY)
#define I2C_UNLOCK()  xSemaphoreGiveRecursive(s_i2c_mtx)

void display_set_backlight(uint8_t brightness);
void display_set_flip(bool flip);

static void ssd1306_cmd(uint8_t cmd)
{
    if (!s_i2c_mtx || !s_dev) return;
    uint8_t buf[2] = { SSD1306_CTRL_CMD, cmd };
    I2C_LOCK();
    i2c_master_transmit(s_dev, buf, 2, 50);
    I2C_UNLOCK();
}

static void ssd1306_cmd2(uint8_t cmd, uint8_t arg)
{
    if (!s_i2c_mtx || !s_dev) return;
    uint8_t buf[3] = { SSD1306_CTRL_CMD, cmd, arg };
    I2C_LOCK();
    i2c_master_transmit(s_dev, buf, 3, 50);
    I2C_UNLOCK();
}

static void ssd1306_data(const uint8_t *data, size_t len)
{
    if (!s_i2c_mtx || !s_dev || len == 0) return;
    // Control byte + payload. Stack buffer is fine for one page (128 B).
    uint8_t buf[1 + 128];
    if (len > 128) len = 128;
    buf[0] = SSD1306_CTRL_DATA;
    memcpy(buf + 1, data, len);
    I2C_LOCK();
    i2c_master_transmit(s_dev, buf, len + 1, 50);
    I2C_UNLOCK();
}

static void ssd1306_apply_orient(void)
{
    // A0/A1 segment remap + C0/C8 COM scan, applied together for 180° flip.
    if (s_flip_on) {
        ssd1306_cmd(0xA0);
        ssd1306_cmd(0xC0);
    } else {
        ssd1306_cmd(0xA1);
        ssd1306_cmd(0xC8);
    }
}

static void ssd1306_init_panel(void)
{
    ssd1306_cmd(0xAE);                 // display off
    ssd1306_cmd2(0xD5, 0x80);          // clock
    ssd1306_cmd2(0xA8, 0x3F);          // multiplex 64
    ssd1306_cmd2(0xD3, 0x00);          // offset
    ssd1306_cmd(0x40);                 // start line 0
    ssd1306_cmd2(0x8D, 0x14);          // charge pump on
    ssd1306_cmd2(0x20, 0x00);          // horizontal addressing
    s_flip_on = settings_get()->display.flip;
    ssd1306_apply_orient();
    ssd1306_cmd2(0xDA, 0x12);          // COM pins
    ssd1306_cmd2(0x81, 0xCF);          // contrast
    ssd1306_cmd2(0xD9, 0xF1);          // precharge
    ssd1306_cmd2(0xDB, 0x40);          // VCOMH
    ssd1306_cmd(0xA4);                 // resume RAM
    ssd1306_cmd(0xA6);                 // normal (not inverted)
    ssd1306_cmd(0xAF);                 // display on
}

static void ssd1306_set_window(int page0, int page1)
{
    uint8_t col[4] = { SSD1306_CTRL_CMD, 0x21, 0x00, (uint8_t)(DISPLAY_WIDTH - 1) };
    uint8_t pg[4]  = { SSD1306_CTRL_CMD, 0x22, (uint8_t)page0, (uint8_t)page1 };
    I2C_LOCK();
    i2c_master_transmit(s_dev, col, 4, 50);
    i2c_master_transmit(s_dev, pg, 4, 50);
    I2C_UNLOCK();
}

static inline uint8_t rgb565_to_1(uint16_t px)
{
    uint8_t r = (px >> 11) & 0x1F;
    uint8_t g = (px >> 5)  & 0x3F;
    uint8_t b =  px        & 0x1F;
    uint8_t r8 = (r << 3) | (r >> 2);
    uint8_t g8 = (g << 2) | (g >> 4);
    uint8_t b8 = (b << 3) | (b >> 2);
    uint8_t luma = (uint8_t)((r8 * 77 + g8 * 150 + b8 * 29) >> 8);
    // 12px bpp4 glyphs are mostly antialiased gray; 128 dropped the strokes.
    return luma >= 96 ? 1 : 0;
}

static void ssd1306_rounder_cb(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 = 0;
    a->x2 = DISPLAY_WIDTH - 1;
    a->y1 &= ~7;
    a->y2 |= 7;
    if (a->y1 < 0) a->y1 = 0;
    if (a->y2 >= DISPLAY_HEIGHT) a->y2 = DISPLAY_HEIGHT - 1;
}

static void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (s_flip_dirty) {
        s_flip_dirty = false;
        ssd1306_apply_orient();
    }

    const int y1 = area->y1;
    const int y2 = area->y2;
    const int page0 = y1 / 8;
    const int page1 = y2 / 8;
    const int rows = y2 - y1 + 1;
    const uint16_t *src = (const uint16_t *)px_map;

    uint8_t page[DISPLAY_WIDTH];
    ssd1306_set_window(page0, page1);

    for (int p = page0; p <= page1; p++) {
        memset(page, 0, sizeof(page));
        int row0 = p * 8 - y1;   // index into src rows
        for (int col = 0; col < DISPLAY_WIDTH; col++) {
            uint8_t bits = 0;
            for (int bit = 0; bit < 8; bit++) {
                int src_row = row0 + bit;
                if (src_row < 0 || src_row >= rows) continue;
                uint16_t px = src[src_row * DISPLAY_WIDTH + col];
                if (rgb565_to_1(px)) bits |= (uint8_t)(1u << bit);
            }
            page[col] = bits;
        }
        ssd1306_data(page, DISPLAY_WIDTH);
    }

    lv_display_flush_ready(disp);
}

void ssd1306_init(void)
{
    s_i2c_mtx = xSemaphoreCreateRecursiveMutex();
    if (!s_i2c_mtx) {
        ESP_LOGE(TAG, "I2C mutex create failed");
        return;
    }

    int sda = g_pins.oled_sda;
    int scl = g_pins.oled_scl;
    if (sda < 0 || scl < 0) {
        ESP_LOGE(TAG, "OLED I2C pins not set");
        return;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = SSD1306_I2C_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));

    if (OLED_PIN_RST >= 0) {
        gpio_set_direction(OLED_PIN_RST, GPIO_MODE_OUTPUT);
        gpio_set_level(OLED_PIN_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(OLED_PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ssd1306_init_panel();

    // 8-line (one page) RGB565 draw buffer in internal RAM.
    const size_t buf_px = DISPLAY_WIDTH * 8;
    lv_color_t *buf = heap_caps_malloc(buf_px * sizeof(lv_color_t),
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "LVGL buffer alloc failed");
        return;
    }

    lv_display_t *disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_add_event_cb(disp, ssd1306_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_set_buffers(disp, buf, NULL,
                           buf_px * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "SSD1306 128x64 I2C initialized (SDA=%d SCL=%d)", sda, scl);
}

void display_set_backlight(uint8_t brightness)
{
    if (!s_i2c_mtx) return;
    if (brightness > 100) brightness = 100;
    if (brightness == 0) {
        ssd1306_cmd(0xAE);
        return;
    }
    ssd1306_cmd(0xAF);
    ssd1306_cmd2(0x81, (uint8_t)((brightness * 255) / 100));
}

void display_set_invert(bool invert)
{
    (void)invert;
}

void display_set_bgr(bool bgr)
{
    (void)bgr;
}

void display_set_flip(bool flip)
{
    s_flip_on    = flip;
    s_flip_dirty = true;
}
