// SPDX-License-Identifier: AGPL-3.0-or-later
// port_clock.cpp - ESP-IDF implementation of hal_clock.h.

#include "esp_rom_sys.h"

#include "hal_clock.h"

extern "C" hal_result_t hal_clock_delay_us(uint32_t us) {
    if (us > HAL_CLOCK_MAX_DELAY_US) return HAL_ERR_INVALID_ARG;
    esp_rom_delay_us(us);
    return HAL_OK;
}
