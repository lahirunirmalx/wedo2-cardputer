// SPDX-License-Identifier: AGPL-3.0-or-later
// keyboard.cpp - see keyboard.hpp.
//
// Wiring (M5Cardputer library, IOMatrix.cpp): three address pins drive a
// 74HC138 that pulls one of 8 lines low; 7 pulled-up inputs read back.
// Line i and input j map to the printed layout as:
//   x = 2*j      when i > 3,  else 2*j + 1      (column 0..13)
//   y = 3 - (i mod 4)                           (row 0 = number row)

#include "keyboard.hpp"

#include "board_cardputer.h"
#include "hal_clock.h"
#include "hal_gpio.h"

namespace kbd {
namespace {
constexpr unsigned kRows = 4;
constexpr unsigned kCols = 14;
constexpr unsigned kLines = 8;
constexpr uint32_t kSettleUs = 20;

constexpr uint8_t kAddrPins[BOARD_KBD_ADDR_COUNT] = BOARD_KBD_ADDR_PINS;
constexpr uint8_t kInputPins[BOARD_KBD_INPUT_COUNT] = BOARD_KBD_INPUT_PINS;

constexpr uint8_t kMap[kRows][kCols] = {
    {'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', kBackspace},
    {kTab, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\'},
    {kFn, kShift, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', kEnter},
    {kCtrl, kOpt, kAlt, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' '},
};

uint64_t g_stable = 0;   // debounced state, bit = y * kCols + x
uint64_t g_prev = 0;     // previous raw sample

uint64_t sample() {
    uint64_t bits = 0;
    for (unsigned i = 0; i < kLines; i++) {
        for (unsigned a = 0; a < BOARD_KBD_ADDR_COUNT; a++) {
            (void)hal_gpio_set(kAddrPins[a], ((i >> a) & 1u) != 0);
        }
        (void)hal_clock_delay_us(kSettleUs);
        for (unsigned j = 0; j < BOARD_KBD_INPUT_COUNT; j++) {
            bool level = true;
            if (hal_gpio_get(kInputPins[j], &level) != HAL_OK || level) continue;  // pressed = low
            const unsigned x = (i > 3) ? 2 * j : 2 * j + 1;
            const unsigned y = 3 - (i & 3u);
            bits |= 1ULL << (y * kCols + x);
        }
    }
    return bits;
}
}  // namespace

bool init() {
    for (uint8_t pin : kAddrPins) {
        const hal_gpio_cfg_t cfg = {pin, HAL_GPIO_DIR_OUT, HAL_GPIO_PULL_NONE, HAL_GPIO_INT_NONE};
        if (hal_gpio_init(&cfg) != HAL_OK || hal_gpio_set(pin, false) != HAL_OK) return false;
    }
    for (uint8_t pin : kInputPins) {
        const hal_gpio_cfg_t cfg = {pin, HAL_GPIO_DIR_IN, HAL_GPIO_PULL_UP, HAL_GPIO_INT_NONE};
        if (hal_gpio_init(&cfg) != HAL_OK) return false;
    }
    g_stable = 0;
    g_prev = 0;
    return true;
}

void scan(Sink sink, void *user) {
    const uint64_t now = sample();
    const bool settled = (now == g_prev);
    g_prev = now;
    if (!settled) return;
    uint64_t changed = now ^ g_stable;
    g_stable = now;
    if (changed == 0 || sink == nullptr) return;
    for (unsigned bit = 0; bit < kRows * kCols; bit++) {
        if ((changed & (1ULL << bit)) == 0) continue;
        const Event ev = {kMap[bit / kCols][bit % kCols], (now & (1ULL << bit)) != 0};
        sink(ev, user);
    }
}

}  // namespace kbd
