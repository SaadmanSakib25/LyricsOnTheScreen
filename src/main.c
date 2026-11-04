#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lyric_fx.h"
#include "lyrics_data.h"

static const char *TAG = "LYRIC_SCREEN";

/* =========================================================================
 * Display & Hardware Configuration
 * ========================================================================= */
#define LCD_WIDTH   128
#define LCD_HEIGHT  160

/* Pin assignments as requested */
#define LCD_MOSI    38
#define LCD_SCLK    39
#define LCD_CS      40
#define LCD_DC      41
#define LCD_RST     42
#define LCD_BL      47

/* ESP32-S3 default BOOT button for interactive playback control */
#define BUTTON_BOOT GPIO_NUM_0

/* Display panel offsets (tweak if your ST7735 module has a 1-2 pixel border shift) */
#define LCD_OFFSET_X 0
#define LCD_OFFSET_Y 0

#define SPI_HOST     SPI2_HOST
#define LCD_DMA_CHUNK  4096
#define LCD_DMA_CHUNKS ((LCD_WIDTH * LCD_HEIGHT * 2 + LCD_DMA_CHUNK - 1) / LCD_DMA_CHUNK)
#define SPI_CLOCK_HZ (15 * 1000 * 1000) // 15 MHz (ST7735 safe rating, eliminates breadboard clock jitter)

/* Backlight brightness (0-255). 255 = 100% steady DC, eliminates PWM switching ripple */
#define LCD_BACKLIGHT_DUTY 255

/* =========================================================================
 * Global State
 * ========================================================================= */
static spi_device_handle_t display;
/* Two RGB565 frames: one is rendered while the other is streamed out by DMA. */
static uint16_t frames[FX_SLOTS][LCD_WIDTH * LCD_HEIGHT];
_Static_assert(LCD_WIDTH == FX_WIDTH && LCD_HEIGHT == FX_HEIGHT, "renderer size must match panel");

typedef struct {
    bool is_playing;
    uint32_t current_ms;
    uint64_t last_timer_us;
    size_t active_index;
    uint32_t animation_tick;
} playback_state_t;

static playback_state_t g_playback = {
    .is_playing = true,
    .current_ms = 0,
    .last_timer_us = 0,
    .active_index = 0,
    .animation_tick = 0,
};

/* =========================================================================
 * Low-level Display Functions
 * ========================================================================= */
static void lcd_command(uint8_t command)
{
    spi_transaction_t transaction = {
        .length = 8,
        .tx_buffer = &command,
    };
    gpio_set_level(LCD_DC, 0);
    ESP_ERROR_CHECK(spi_device_transmit(display, &transaction));
}

static void lcd_data(const void *data, size_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    gpio_set_level(LCD_DC, 1);
    while (length > 0) {
        size_t chunk = (length > 4096) ? 4096 : length;
        spi_transaction_t transaction = {
            .length = chunk * 8,
            .tx_buffer = p,
        };
        ESP_ERROR_CHECK(spi_device_transmit(display, &transaction));
        p += chunk;
        length -= chunk;
    }
}

static void lcd_reset(void)
{
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(25));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void lcd_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t columns[] = {
        0, (uint8_t)(x0 + LCD_OFFSET_X),
        0, (uint8_t)(x1 + LCD_OFFSET_X)
    };
    uint8_t rows[] = {
        0, (uint8_t)(y0 + LCD_OFFSET_Y),
        0, (uint8_t)(y1 + LCD_OFFSET_Y)
    };
    lcd_command(0x2A);
    lcd_data(columns, sizeof(columns));
    lcd_command(0x2B);
    lcd_data(rows, sizeof(rows));
    lcd_command(0x2C);
}

static void lcd_init(void)
{
    gpio_config_t control = {
        .pin_bit_mask = (1ULL << LCD_DC) | (1ULL << LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&control));

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = LCD_CS,
        .queue_size = LCD_DMA_CHUNKS + 2,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &device_config, &display));

    lcd_reset();

    // 1. Software Reset
    lcd_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    // 2. Sleep Out
    lcd_command(0x11);
    vTaskDelay(pdMS_TO_TICKS(200));

    // 3. Frame Rate Control (FRMCTR1 - Normal mode): ~60 Hz stable refresh
    const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    lcd_command(0xB1);
    lcd_data(frmctr1, sizeof(frmctr1));

    // 4. Frame Rate Control (FRMCTR2 - Idle mode)
    const uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    lcd_command(0xB2);
    lcd_data(frmctr2, sizeof(frmctr2));

    // 5. Frame Rate Control (FRMCTR3 - Partial mode)
    const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    lcd_command(0xB3);
    lcd_data(frmctr3, sizeof(frmctr3));

    // 6. Display Inversion Control (INVCTR - 0x07 = Dot/Line inversion)
    // CRITICAL: 0x00 causes severe whole-frame polarity flicker ("shaking"). 0x07 enables dot inversion.
    const uint8_t invctr = 0x07;
    lcd_command(0xB4);
    lcd_data(&invctr, 1);

    // 7. Power Control 1 (PWCTR1 - AVDD / GVDD / VGH / VGL)
    const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    lcd_command(0xC0);
    lcd_data(pwctr1, sizeof(pwctr1));

    // 8. Power Control 2 (PWCTR2)
    const uint8_t pwctr2[] = {0xC5};
    lcd_command(0xC1);
    lcd_data(pwctr2, sizeof(pwctr2));

    // 9. Power Control 3 (PWCTR3)
    const uint8_t pwctr3[] = {0x0A, 0x00};
    lcd_command(0xC2);
    lcd_data(pwctr3, sizeof(pwctr3));

    // 10. Power Control 4 (PWCTR4)
    const uint8_t pwctr4[] = {0x8A, 0x2A};
    lcd_command(0xC3);
    lcd_data(pwctr4, sizeof(pwctr4));

    // 11. Power Control 5 (PWCTR5)
    const uint8_t pwctr5[] = {0x8A, 0xEE};
    lcd_command(0xC4);
    lcd_data(pwctr5, sizeof(pwctr5));

    // 12. VCOM Control (VMCTR1 - VCOM voltage cancellation)
    const uint8_t vmctr1[] = {0x0E};
    lcd_command(0xC5);
    lcd_data(vmctr1, sizeof(vmctr1));

    // 13. Display Inversion Off (Normal display)
    lcd_command(0x20);

    // 14. Memory Data Access Control (MADCTL - Orientation)
    const uint8_t orientation = 0x00;
    lcd_command(0x36);
    lcd_data(&orientation, 1);

    // 15. Interface Pixel Format (COLMOD - 16-bit RGB565)
    const uint8_t color_mode = 0x05;
    lcd_command(0x3A);
    lcd_data(&color_mode, 1);

    // 16. Gamma Adjustments (Positive & Negative curves)
    const uint8_t gmctrp1[] = {
        0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
        0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10
    };
    lcd_command(0xE0);
    lcd_data(gmctrp1, sizeof(gmctrp1));

    const uint8_t gmctrn1[] = {
        0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
        0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10
    };
    lcd_command(0xE1);
    lcd_data(gmctrn1, sizeof(gmctrn1));

    // 17. Normal Display Mode On
    lcd_command(0x13);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 18. Display On
    lcd_command(0x29);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = LCD_BACKLIGHT_DUTY,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static spi_transaction_t s_frame_trans[LCD_DMA_CHUNKS];
static int s_frame_inflight;

/* Block until the previously queued frame has been fully sent. */
static void lcd_wait_frame_done(void)
{
    while (s_frame_inflight > 0) {
        spi_transaction_t *done;
        ESP_ERROR_CHECK(spi_device_get_trans_result(display, &done, portMAX_DELAY));
        s_frame_inflight--;
    }
}

/* Start streaming a frame to the panel in the background (DMA). */
static void lcd_present_async(uint16_t *buf)
{
    /* ST7735 expects big-endian RGB565; the buffer is redrawn from scratch next time */
    for (size_t i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
        buf[i] = (uint16_t)((buf[i] >> 8) | (buf[i] << 8));
    }
    lcd_wait_frame_done();
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    gpio_set_level(LCD_DC, 1);

    const uint8_t *p = (const uint8_t *)buf;
    size_t remaining = LCD_WIDTH * LCD_HEIGHT * 2;
    for (int n = 0; remaining > 0; n++) {
        size_t chunk = remaining > LCD_DMA_CHUNK ? LCD_DMA_CHUNK : remaining;
        spi_transaction_t *t = &s_frame_trans[n];
        memset(t, 0, sizeof(*t));
        t->length = chunk * 8;
        t->tx_buffer = p;
        ESP_ERROR_CHECK(spi_device_queue_trans(display, t, portMAX_DELAY));
        s_frame_inflight++;
        p += chunk;
        remaining -= chunk;
    }
}

/* =========================================================================
 * Lyric Lookup
 * ========================================================================= */
static size_t find_active_lyric(uint32_t position_ms)
{
    size_t active = 0;
    for (size_t i = 0; i < LYRICS_COUNT; i++) {
        if (position_ms >= lyrics[i].start_ms) {
            active = i;
        } else {
            break;
        }
    }
    return active;
}

/* =========================================================================
 * User Interaction & Controls (BOOT Button + Serial CLI)
 * ========================================================================= */
static void button_init(void)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_BOOT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
}

static void handle_button_input(playback_state_t *ps)
{
    static uint32_t press_start_ms = 0;
    static bool was_pressed = false;

    bool is_pressed = (gpio_get_level(BUTTON_BOOT) == 0);
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (is_pressed && !was_pressed) {
        press_start_ms = now_ms;
        was_pressed = true;
    } else if (!is_pressed && was_pressed) {
        uint32_t duration = now_ms - press_start_ms;
        was_pressed = false;

        if (duration >= 1200) {
            // Long press: restart song from 00:00
            ps->current_ms = 0;
            ps->active_index = 0;
            ps->is_playing = true;
            ps->last_timer_us = esp_timer_get_time();
            ESP_LOGI(TAG, "Song restarted from beginning (00:00)");
        } else if (duration >= 50) {
            // Short press: Play/Pause toggle
            ps->is_playing = !ps->is_playing;
            if (ps->is_playing) {
                ps->last_timer_us = esp_timer_get_time();
                ESP_LOGI(TAG, "Playback RESUMED at %lu ms", (unsigned long)ps->current_ms);
            } else {
                ESP_LOGI(TAG, "Playback PAUSED at %lu ms", (unsigned long)ps->current_ms);
            }
        }
    }
}

static void serial_init(void)
{
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

static void handle_serial_input(playback_state_t *ps)
{
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) > 0) {
        if (c == ' ') {
            ps->is_playing = !ps->is_playing;
            if (ps->is_playing) {
                ps->last_timer_us = esp_timer_get_time();
                ESP_LOGI(TAG, "Serial: Playback RESUMED at %lu ms", (unsigned long)ps->current_ms);
            } else {
                ESP_LOGI(TAG, "Serial: Playback PAUSED at %lu ms", (unsigned long)ps->current_ms);
            }
        } else if (c == 'r' || c == 'R') {
            ps->current_ms = 0;
            ps->active_index = 0;
            ps->is_playing = true;
            ps->last_timer_us = esp_timer_get_time();
            ESP_LOGI(TAG, "Serial: Song RESTARTED from 00:00");
        } else if (c == '+' || c == '=') {
            ps->current_ms += 2000;
            if (ps->current_ms > SONG_TOTAL_DURATION_MS) ps->current_ms = SONG_TOTAL_DURATION_MS;
            ESP_LOGI(TAG, "Serial: Nudge Forward +2s -> %02lu:%02lu",
                     (unsigned long)((ps->current_ms / 1000) / 60),
                     (unsigned long)((ps->current_ms / 1000) % 60));
        } else if (c == '-' || c == '_') {
            if (ps->current_ms >= 2000) ps->current_ms -= 2000;
            else ps->current_ms = 0;
            ESP_LOGI(TAG, "Serial: Nudge Backward -2s -> %02lu:%02lu",
                     (unsigned long)((ps->current_ms / 1000) / 60),
                     (unsigned long)((ps->current_ms / 1000) % 60));
        } else if (c >= '0' && c <= '9') {
            int pct = (c - '0') * 10;
            ps->current_ms = (uint32_t)((uint64_t)SONG_TOTAL_DURATION_MS * pct / 100);
            ESP_LOGI(TAG, "Serial: Jump to %d%% -> %02lu:%02lu",
                     pct,
                     (unsigned long)((ps->current_ms / 1000) / 60),
                     (unsigned long)((ps->current_ms / 1000) % 60));
        }
    }
}

/* =========================================================================
 * Two-core render pipeline
 *   core 1, draw task : playback clock, input, fx_draw_frame()   (frame N+1)
 *   core 0, app_main  : fx_finish_frame() + DMA to the panel     (frame N)
 * A frame buffer travels  free_q -> draw -> ready_q -> finish/send -> free_q
 * ========================================================================= */
#define FRAME_MS 40 /* 25 FPS */

typedef struct {
    int slot;
    fx_post_t post;
    int64_t draw_us;
} frame_msg_t;

static QueueHandle_t s_free_q, s_ready_q;

static void draw_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL)); /* this task feeds the watchdog itself */

    size_t last_logged_index = SIZE_MAX;
    g_playback.last_timer_us = esp_timer_get_time();
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        uint64_t now_us = esp_timer_get_time();

        // Advance playback clock if playing
        if (g_playback.is_playing) {
            uint32_t delta_ms = (uint32_t)((now_us - g_playback.last_timer_us) / 1000);
            // Keep the sub-millisecond remainder so the clock doesn't drift behind the audio
            g_playback.last_timer_us += (uint64_t)delta_ms * 1000;
            g_playback.current_ms += delta_ms;

            if (g_playback.current_ms >= SONG_TOTAL_DURATION_MS + FX_END_HOLD_MS) {
                // Auto-loop back to the start after the closing title card
                g_playback.current_ms = 0;
                g_playback.active_index = 0;
                ESP_LOGI(TAG, "Song completed, looping to start...");
            }
        } else {
            g_playback.last_timer_us = now_us;
        }

        g_playback.active_index = find_active_lyric(g_playback.current_ms);
        if (g_playback.active_index != last_logged_index) {
            last_logged_index = g_playback.active_index;
            uint32_t s = g_playback.current_ms / 1000;
            ESP_LOGI(TAG, "[%02lu:%02lu] %s",
                     (unsigned long)(s / 60), (unsigned long)(s % 60),
                     lyrics[g_playback.active_index].text);
        }

        handle_button_input(&g_playback);
        handle_serial_input(&g_playback);

        // Draw the next frame into a free buffer while core 0 finishes/sends the last one
        frame_msg_t msg;
        xQueueReceive(s_free_q, &msg.slot, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time();
        fx_draw_frame(msg.slot, frames[msg.slot], g_playback.current_ms, g_playback.active_index,
                      g_playback.animation_tick, g_playback.is_playing, &msg.post);
        msg.draw_us = esp_timer_get_time() - t0;
        xQueueSend(s_ready_q, &msg, portMAX_DELAY);
        g_playback.animation_tick++;

        esp_task_wdt_reset();
        if (xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_MS)) == pdFALSE) {
            last_wake = xTaskGetTickCount(); /* running late: don't try to catch up */
        }
    }
}

static void watchdog_setup(void)
{
    /* Only watch core 0's idle task: core 1 is busy drawing and feeds the WDT itself. */
    esp_task_wdt_config_t cfg = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = BIT(0),
#ifdef CONFIG_ESP_TASK_WDT_PANIC
        .trigger_panic = true,
#else
        .trigger_panic = false,
#endif
    };
    esp_err_t err = esp_task_wdt_reconfigure(&cfg);
    if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_init(&cfg); /* WDT wasn't started */
    ESP_ERROR_CHECK(err);
}

/* =========================================================================
 * Application Entry Point
 * ========================================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "Initializing ST7735 Display (128x160)...");
    lcd_init();
    backlight_init();
    fx_init();
    button_init();
    serial_init();
    watchdog_setup();

    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  Something Just Like This - Lyrical Screen    ");
    ESP_LOGI(TAG, "  Coldplay & The Chainsmokers                  ");
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "Controls:");
    ESP_LOGI(TAG, "  - BOOT Button: Short click = Play/Pause | Hold (>1.2s) = Restart");
    ESP_LOGI(TAG, "  - Serial Monitor (115200 baud):");
    ESP_LOGI(TAG, "      [Space] = Play/Pause");
    ESP_LOGI(TAG, "      'r'     = Restart from beginning (00:00)");
    ESP_LOGI(TAG, "      '+'/'-' = Nudge sync (+2s / -2s)");
    ESP_LOGI(TAG, "      '0'-'9' = Seek (0%% to 90%%)");
    ESP_LOGI(TAG, "===============================================");

    g_playback.is_playing = true;
    g_playback.current_ms = 0;
    g_playback.active_index = 0;
    g_playback.animation_tick = 0;

    s_free_q = xQueueCreate(FX_SLOTS, sizeof(int));
    s_ready_q = xQueueCreate(FX_SLOTS, sizeof(frame_msg_t));
    for (int i = 0; i < FX_SLOTS; i++) xQueueSend(s_free_q, &i, 0);
    xTaskCreatePinnedToCore(draw_task, "draw", 6144, NULL, 5, NULL, 1);

    /* core 0: finish each drawn frame and stream it to the panel */
    int prev_slot = -1;
    int64_t draw_sum = 0, draw_max = 0, fin_sum = 0, fin_max = 0;
    int frames_n = 0;
    int64_t stats_t0 = esp_timer_get_time();

    while (true) {
        frame_msg_t msg;
        xQueueReceive(s_ready_q, &msg, portMAX_DELAY);

        int64_t t0 = esp_timer_get_time();
        fx_finish_frame(msg.slot, frames[msg.slot], &msg.post);
        int64_t fin_us = esp_timer_get_time() - t0;

        lcd_present_async(frames[msg.slot]); /* waits for the previous frame's DMA first */
        if (prev_slot >= 0) xQueueSend(s_free_q, &prev_slot, portMAX_DELAY);
        prev_slot = msg.slot;

        // Report render cost every ~10 s so slow scenes are easy to spot
        draw_sum += msg.draw_us;
        fin_sum += fin_us;
        if (msg.draw_us > draw_max) draw_max = msg.draw_us;
        if (fin_us > fin_max) fin_max = fin_us;
        if (++frames_n >= 250) {
            int64_t now = esp_timer_get_time();
            ESP_LOGI(TAG, "%.1f fps | draw (core 1) avg %.1f / max %.1f ms | finish (core 0) avg %.1f / max %.1f ms | budget %d ms",
                     frames_n * 1e6 / (double)(now - stats_t0),
                     draw_sum / 1000.0 / frames_n, draw_max / 1000.0,
                     fin_sum / 1000.0 / frames_n, fin_max / 1000.0, FRAME_MS);
            draw_sum = draw_max = fin_sum = fin_max = 0;
            frames_n = 0;
            stats_t0 = now;
        }
    }
}
