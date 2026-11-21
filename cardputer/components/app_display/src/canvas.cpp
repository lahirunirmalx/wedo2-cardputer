// SPDX-License-Identifier: AGPL-3.0-or-later
// canvas.cpp - the off-screen frame and the drawing primitives of
// display.hpp. Portable C++: no HAL or ESP-IDF header, so the host preview
// (test/host/render_preview.cpp) compiles this file as it is.

#include <cstdlib>
#include <cstring>

#include "display.hpp"
#include "display_frame.hpp"
#include "font5x7.h"

namespace display {
namespace {
uint8_t g_frame[detail::kStride * kHeight];

inline void put(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
    uint8_t *p = &g_frame[static_cast<size_t>(y) * detail::kStride + static_cast<size_t>(x) * 2];
    p[0] = static_cast<uint8_t>(color >> 8);
    p[1] = static_cast<uint8_t>(color);
}

// Horizontal run, clipped.
void span(int x0, int x1, int y, uint16_t color) {
    if (y < 0 || y >= kHeight) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= kWidth) x1 = kWidth - 1;
    for (int x = x0; x <= x1; x++) put(x, y, color);
}
}  // namespace

namespace detail {
const uint8_t *frame() { return g_frame; }
}  // namespace detail

void fill_rect(int x, int y, int w, int h, uint16_t color) {
    for (int yy = y; yy < y + h; yy++) span(x, x + w - 1, yy, color);
}

void round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int yy = 0; yy < h; yy++) {
        int inset = 0;
        const int dy = yy < r ? r - 1 - yy : (yy >= h - r ? yy - (h - r) : -1);
        if (dy >= 0) {
            // Corner rows: pull the run in until it is inside the corner circle.
            while (inset < r && (r - 1 - inset) * (r - 1 - inset) + dy * dy > (r - 1) * (r - 1) + (r - 1)) inset++;
        }
        span(x + inset, x + w - 1 - inset, y + yy, color);
    }
}

void fill_circle(int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r + r / 2) put(cx + dx, cy + dy, color);
        }
    }
}

void ring(int cx, int cy, int r_outer, int r_inner, uint16_t color) {
    for (int dy = -r_outer; dy <= r_outer; dy++) {
        for (int dx = -r_outer; dx <= r_outer; dx++) {
            const int d2 = dx * dx + dy * dy;
            if (d2 <= r_outer * r_outer + r_outer / 2 && d2 > r_inner * r_inner) put(cx + dx, cy + dy, color);
        }
    }
}

void line(int x0, int y0, int x1, int y1, int thickness, uint16_t color) {
    const int rad = thickness > 1 ? (thickness - 1) / 2 : 0;
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (rad == 0) {
            put(x0, y0, color);
        } else {
            fill_circle(x0, y0, rad, color);
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void text(int x, int y, const char *s, int scale, uint16_t color) {
    if (s == nullptr || scale <= 0) return;
    for (; *s != '\0'; s++, x += kCellW * scale) {
        const uint8_t *g = font5x7_glyph(*s);
        for (int gx = 0; gx < 5; gx++) {
            for (int gy = 0; gy < 7; gy++) {
                if (((g[gx] >> gy) & 1u) == 0) continue;
                fill_rect(x + gx * scale, y + gy * scale, scale, scale, color);
            }
        }
    }
}

int text_width(const char *s, int scale) {
    if (s == nullptr) return 0;
    return static_cast<int>(std::strlen(s)) * kCellW * scale;
}

}  // namespace display
