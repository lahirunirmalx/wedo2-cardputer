// SPDX-License-Identifier: AGPL-3.0-or-later
// display.cpp - the device half of display.hpp: panel bring-up and
// present(), glue between st7789.c and hal_lcd.h. The frame and the drawing
// primitives are in canvas.cpp.

#include "display.hpp"

#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_cardputer.h"
#include "display_frame.hpp"
#include "hal_lcd.h"
#include "st7789.h"
#include "theme.hpp"

namespace display {
namespace {
constexpr char TAG[] = "display";

st7789_t g_lcd;
bool g_ok = false;

bool lcd_cmd(void *, uint8_t cmd, const uint8_t *params, size_t len) {
    return hal_lcd_cmd(cmd, params, len) == HAL_OK;
}
bool lcd_data(void *, const uint8_t *data, size_t len) { return hal_lcd_data(data, len) == HAL_OK; }
void lcd_delay(void *, uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

}  // namespace

bool init() {
    const hal_lcd_cfg_t pins = {BOARD_LCD_SCK, BOARD_LCD_MOSI, BOARD_LCD_CS, BOARD_LCD_DC,
                                BOARD_LCD_RST, BOARD_LCD_BL,   BOARD_LCD_HZ};
    hal_result_t r = hal_lcd_init(&pins);
    if (r == HAL_OK) r = hal_lcd_reset();
    if (r != HAL_OK) {
        ESP_LOGE(TAG, "LCD bus init failed (%s)", hal_result_name(r));
        return false;
    }
    std::memset(&g_lcd, 0, sizeof g_lcd);
    g_lcd.cmd = lcd_cmd;
    g_lcd.data = lcd_data;
    g_lcd.delay = lcd_delay;
    g_lcd.width = BOARD_LCD_W;
    g_lcd.height = BOARD_LCD_H;
    g_lcd.x_off = BOARD_LCD_X_OFF;
    g_lcd.y_off = BOARD_LCD_Y_OFF;
    g_lcd.madctl = BOARD_LCD_MADCTL;
    g_lcd.invert = true;
    if (!st7789_init(&g_lcd)) {
        ESP_LOGE(TAG, "LCD init failed");
        return false;
    }
    g_ok = true;
    fill_rect(0, 0, kWidth, kHeight, theme::palette().bg);
    if (!present(0, 0, kWidth, kHeight) || hal_lcd_backlight(true) != HAL_OK) {
        ESP_LOGE(TAG, "LCD first frame failed");
        g_ok = false;
        return false;
    }
    ESP_LOGI(TAG, "LCD ok: st7789 %dx%d", kWidth, kHeight);
    return true;
}

bool present(int x, int y, int w, int h) {
    if (!g_ok) return false;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= kWidth || y >= kHeight || w <= 0 || h <= 0) return true;
    if (w > kWidth - x) w = kWidth - x;
    if (h > kHeight - y) h = kHeight - y;
    const uint8_t *src = detail::frame() + static_cast<size_t>(y) * detail::kStride + static_cast<size_t>(x) * 2;
    return st7789_blit(&g_lcd, static_cast<unsigned>(x), static_cast<unsigned>(y), static_cast<unsigned>(w),
                       static_cast<unsigned>(h), src, detail::kStride);
}

}  // namespace display
