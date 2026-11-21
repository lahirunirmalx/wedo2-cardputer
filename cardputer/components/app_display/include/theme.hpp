// SPDX-License-Identifier: AGPL-3.0-or-later
// theme.hpp - the only place colours are defined. Screens pick a role or a
// surface from the palette, never a colour value. The hub LED presets live
// here too, with the colour used to show each one on screen.

#pragma once

#include <cstdint>

namespace theme {

enum class Role : uint8_t {
    Fg = 0,   // ordinary text
    Title,    // header line
    Accent,   // sensor values
    Good,     // connected, motor running
    Warn,     // scanning, waiting for the user, locked run
    Bad,      // errors
    Dim,      // labels, empty ports, idle icons
    Count
};

struct Palette {
    uint16_t bg;                                          // screen background, RGB565
    uint16_t card;                                        // card surface
    uint16_t edge;                                        // outlines, guide lines
    uint16_t track;                                       // empty part of bars and gauges
    uint16_t fg[static_cast<unsigned>(Role::Count)];      // per role
    uint16_t of(Role r) const { return fg[static_cast<unsigned>(r)]; }
};

/** The active palette. */
const Palette &palette();

struct LedPreset {
    const char *name;
    uint8_t r, g, b;      // sent to the hub LED
    uint16_t swatch;      // RGB565 shown on screen
};

constexpr unsigned kLedPresetCount = 10;

/** Preset 0 is "off"; 1..9 are colours. Out-of-range gives preset 0. */
const LedPreset &led_preset(unsigned index);

}  // namespace theme
