/*
 * display.c — hardware init + start of lvgl_task
 *
 * Changes vs the old version:
 *  - ui_init() → ui_manager_init() (before task start)
 *  - while(1){lv_timer_handler()} → ui_manager_run() (blocks)
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "display.h"
#include "board_def.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "ui_manager.h"

static const char *TAG = "DISPLAY";

void lvgl_tick_init(void);
static void lvgl_task(void *arg);

void display_init(void)
{
    lv_init();
    lvgl_tick_init();

#if CONFIG_DISPLAY_ILI9341
    extern void ili9341_init(void);
    ili9341_init();
#elif CONFIG_DISPLAY_ST7789V
    extern void st7789v_init(void);
    st7789v_init();
#elif CONFIG_DISPLAY_CO5300
    extern void co5300_init(void);
    co5300_init();
#elif CONFIG_DISPLAY_ST7796
    extern void st7796_init(void);
    st7796_init();
#elif CONFIG_DISPLAY_ILI9488
    extern void ili9488_init(void);
    ili9488_init();
#elif CONFIG_DISPLAY_SSD1322
    extern void ssd1322_init(void);
    ssd1322_init();
#elif CONFIG_DISPLAY_SSD1306
    extern void ssd1306_init(void);
    ssd1306_init();
#else
    #error "Unknown DISPLAY_TYPE"
#endif

    // The driver's lv_display_create() auto-shows LVGL's perf monitor. Its
    // visibility now follows settings.display.show_fps and is applied by
    // ui_manager_run() on the LVGL task (see UI_EVT_FPS_CHANGED).

    // Initialize the manager BEFORE starting the task (creates queue, registers callback)
    ui_manager_init();

    ESP_LOGI(TAG, "Display initialized");
}

void display_start(void)
{
#if CONFIG_DISPLAY_SSD1306
    const UBaseType_t prio = 4;    // yield to audio decode (prio 5) — I2C OLED flushes are slow
#else
    const UBaseType_t prio = 5;    // same prio as the audio decode tasks on CPU1 → fair time-slicing.
#endif
    // 16 KB > SPIRAM_MALLOC_ALWAYSINTERNAL (2 KB), so default xTaskCreate puts
    // this stack in PSRAM. Playlist/settings SPIFFS writes from LVGL then
    // reboot (cache disabled while the stack is unmapped). Prefer DRAM.
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        lvgl_task, "lvgl", 16384, NULL, prio, NULL, 1,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "lvgl DRAM stack failed — falling back (SPIFFS from UI is unsafe)");
        xTaskCreatePinnedToCore(lvgl_task, "lvgl", 16384, NULL, prio, NULL, 1);
    }
}

static void lvgl_task(void *arg)
{
    // ui_manager_run() creates the startup screen and runs the LVGL loop + event queue.
    // This function never returns.
    ui_manager_run();
}

static void lv_tick_task(void *arg)
{
    lv_tick_inc(1);
}

void lvgl_tick_init(void)
{
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "lv_tick"
    };

    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000));
}
