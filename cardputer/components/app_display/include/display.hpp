// SPDX-License-Identifier: AGPL-3.0-or-later
// display.hpp - the Cardputer LCD as an off-screen frame plus present().
//
// Draw into the frame with the primitives below, then send the changed
// rectangle to the panel with present(). Everything is clipped to the
// frame. Colours are RGB565 values taken from theme.hpp, never literals.
// One task owns the display.

#pragma once

#include <cstdint>

namespace display {

constexpr int kWidth = 240;
constexpr int kHeight = 135;
constexpr int kCellW = 6;   // text cell in px at scale 1 (5x7 glyph + gap)
constexpr int kCellH = 8;

/** Bring up the bus and the panel, clear to the theme background, light up. */
bool init();

void fill_rect(int x, int y, int w, int h, uint16_t color);
/** Filled rectangle with corner radius @p r. */
void round_rect(int x, int y, int w, int h, int r, uint16_t color);
void fill_circle(int cx, int cy, int r, uint16_t color);
/** Circle band between @p r_inner (exclusive) and @p r_outer (inclusive). */
void ring(int cx, int cy, int r_outer, int r_inner, uint16_t color);
/** Line of roughly @p thickness px. */
void line(int x0, int y0, int x1, int y1, int thickness, uint16_t color);
/** Text with a transparent background; top-left at (x, y). */
void text(int x, int y, const char *s, int scale, uint16_t color);
int text_width(const char *s, int scale);

/** Send one rectangle of the frame to the panel. */
bool present(int x, int y, int w, int h);

}  // namespace display
