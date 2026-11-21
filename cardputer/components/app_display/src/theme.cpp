// SPDX-License-Identifier: AGPL-3.0-or-later
// theme.cpp - see theme.hpp. Dark theme, RGB565. Palette values follow the
// Braid demo's disp_theme.c.

#include "theme.hpp"

namespace theme {
namespace {

constexpr uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

constexpr LedPreset led(const char *name, unsigned r, unsigned g, unsigned b) {
    return {name, static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), rgb565(r, g, b)};
}

constexpr Palette kDark = {
    rgb565(0x08, 0x0A, 0x0E),  // bg
    rgb565(0x1C, 0x21, 0x2B),  // card
    rgb565(0x34, 0x3B, 0x49),  // edge
    rgb565(0x2A, 0x31, 0x3E),  // track
    {
        rgb565(0xE0, 0xE0, 0xE0),  // Fg
        rgb565(0x4F, 0xC3, 0xF7),  // Title
        rgb565(0xFF, 0xD5, 0x4F),  // Accent
        rgb565(0x66, 0xBB, 0x6A),  // Good
        rgb565(0xFF, 0xA7, 0x26),  // Warn
        rgb565(0xEF, 0x53, 0x50),  // Bad
        rgb565(0x90, 0x90, 0x90),  // Dim
    },
};

constexpr LedPreset kLeds[kLedPresetCount] = {
    {"OFF", 0, 0, 0, rgb565(0x60, 0x60, 0x60)},
    led("RED", 0xFF, 0x00, 0x00),
    led("ORANGE", 0xFF, 0x50, 0x00),
    led("YELLOW", 0xFF, 0xC8, 0x00),
    led("GREEN", 0x00, 0xFF, 0x00),
    led("CYAN", 0x00, 0xFF, 0xFF),
    led("BLUE", 0x00, 0x00, 0xFF),
    led("PURPLE", 0x80, 0x00, 0xFF),
    led("PINK", 0xFF, 0x00, 0x80),
    led("WHITE", 0xFF, 0xFF, 0xFF),
};

}  // namespace

const Palette &palette() { return kDark; }

const LedPreset &led_preset(unsigned index) { return kLeds[index < kLedPresetCount ? index : 0]; }

}  // namespace theme
