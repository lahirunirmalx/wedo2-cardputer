// SPDX-License-Identifier: AGPL-3.0-or-later
// screen.cpp - see screen.hpp.

#include "screen.hpp"

#include <cmath>
#include <cstdio>

#include "display.hpp"
#include "theme.hpp"

namespace screen {
namespace {
using theme::Role;

// --- layout ------------------------------------------------------------------
constexpr int kMargin = 4;
constexpr int kRadius = 6;
constexpr int kPad = 8;                 // text inset inside a card

constexpr int kBarH = 20;
constexpr int kPillW = 98;
constexpr int kPillH = 16;

constexpr int kMainY = 22;
constexpr int kMainH = 70;
constexpr int kPortW = 114;
constexpr int kWideW = display::kWidth - 2 * kMargin;

constexpr int kLowY = 95;
constexpr int kLowH = 36;
constexpr int kPowerX = 4;
constexpr int kPowerW = 74;
constexpr int kLedX = 82;
constexpr int kLedW = 76;
constexpr int kToneX = 162;
constexpr int kToneW = 74;

constexpr int kToneBars = 4;
constexpr int kSpinDots = 8;
constexpr float kPi = 3.14159265f;

// --- animation state -----------------------------------------------------------
uint32_t g_frame = 0;
int g_angle[2] = {0, 0};                // rotor angle per port, degrees

// --- what is on the panel (0 = nothing) ----------------------------------------
uint64_t g_shown_bar = 0;
uint64_t g_shown_main[2] = {0, 0};
uint64_t g_shown_wide = 0;
uint64_t g_shown_power = 0;
uint64_t g_shown_led = 0;
uint64_t g_shown_tone = 0;
int g_shown_mode = -1;                  // 1 = port cards, 0 = wide card

uint16_t col(Role r) { return theme::palette().of(r); }

void card(int x, int y, int w, int h) { display::round_rect(x, y, w, h, kRadius, theme::palette().card); }

void label(int x, int y, const char *s, Role role = Role::Dim) { display::text(x, y, s, 1, col(role)); }

void label_right(int right, int y, const char *s, Role role = Role::Dim) {
    display::text(right - display::text_width(s, 1), y, s, 1, col(role));
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// --- top bar -------------------------------------------------------------------
void draw_bar(const Model &m) {
    const theme::Palette &pal = theme::palette();
    display::fill_rect(0, 0, display::kWidth, kBarH, pal.bg);
    display::text(6, 2, "WEDO 2.0", 2, col(Role::Title));

    const char *text = "IDLE";
    Role role = Role::Dim;
    switch (m.link) {
        case Link::Scanning:   text = "SCANNING";   role = Role::Warn; break;
        case Link::Connecting: text = "CONNECTING"; role = Role::Warn; break;
        case Link::Setup:      text = "SETUP";      role = Role::Warn; break;
        case Link::Ready:      text = "CONNECTED";  role = Role::Good; break;
        default: break;
    }
    const int px = display::kWidth - kMargin - kPillW;
    display::round_rect(px, 2, kPillW, kPillH, kPillH / 2, pal.card);
    const bool busy = m.link != Link::Ready && m.link != Link::Idle;
    const int dot = busy ? 2 + static_cast<int>((g_frame / 4) % 3) : 4;   // pulses while busy
    display::fill_circle(px + 11, 2 + kPillH / 2, dot, col(role));
    display::text(px + 20, 6, text, 1, col(role));
}

// --- port cards ------------------------------------------------------------------
void draw_rotor(int cx, int cy, int angle_deg, bool running) {
    const uint16_t c = col(running ? Role::Good : Role::Dim);
    display::ring(cx, cy, 19, 16, c);
    for (int k = 0; k < 3; k++) {
        const float a = static_cast<float>(angle_deg + 120 * k) * kPi / 180.0f;
        const int ex = cx + static_cast<int>(std::lround(15.0f * std::cos(a)));
        const int ey = cy + static_cast<int>(std::lround(15.0f * std::sin(a)));
        display::line(cx, cy, ex, ey, 3, c);
    }
    display::fill_circle(cx, cy, 5, c);
    display::fill_circle(cx, cy, 2, theme::palette().card);
}

void draw_port(unsigned index, const Port &p) {
    const theme::Palette &pal = theme::palette();
    const int x = kMargin + static_cast<int>(index) * (kPortW + kMargin);
    const int y = kMainY;
    const int right = x + kPortW - kPad;
    char text[16];

    if (p.kind == PortKind::Empty) {
        // Outline only: an empty socket.
        display::round_rect(x, y, kPortW, kMainH, kRadius, pal.edge);
        display::round_rect(x + 1, y + 1, kPortW - 2, kMainH - 2, kRadius - 1, pal.bg);
    } else {
        card(x, y, kPortW, kMainH);
    }
    std::snprintf(text, sizeof text, "PORT %u", index + 1u);
    label(x + kPad, y + 6, text);

    switch (p.kind) {
        case PortKind::Empty: {
            const char *s = "EMPTY";
            display::text(x + (kPortW - display::text_width(s, 2)) / 2, y + 30, s, 2, pal.edge);
            break;
        }
        case PortKind::Motor: {
            const bool running = p.power != 0;
            label_right(right, y + 6, "MOTOR", running ? Role::Good : Role::Dim);
            draw_rotor(x + 30, y + 43, g_angle[index], running);
            if (running) {
                display::text(x + 58, y + 24, p.power > 0 ? "FWD" : "REV", 2, col(Role::Good));
                std::snprintf(text, sizeof text, "%d%%", p.power > 0 ? p.power : -p.power);
                display::text(x + 58, y + 44, text, 2, col(Role::Fg));
            } else {
                display::text(x + 58, y + 34, "STOP", 2, col(Role::Dim));
            }
            break;
        }
        case PortKind::Tilt: {
            label_right(right, y + 6, "TILT", Role::Accent);
            const int bx = x + kPad;
            const int by = y + 19;
            const int size = 46;
            display::round_rect(bx, by, size, size, 6, pal.track);
            display::fill_rect(bx + size / 2, by + 5, 1, size - 10, pal.edge);
            display::fill_rect(bx + 5, by + size / 2, size - 10, 1, pal.edge);
            if (p.has_value) {
                const int dx = clampi(p.tilt_x, -45, 45) * 17 / 45;
                const int dy = clampi(p.tilt_y, -45, 45) * 17 / 45;
                display::fill_circle(bx + size / 2 + dx, by + size / 2 + dy, 5, col(Role::Accent));
                std::snprintf(text, sizeof text, "X%3d", clampi(p.tilt_x, -99, 99));
                display::text(x + 62, y + 24, text, 2, col(Role::Fg));
                std::snprintf(text, sizeof text, "Y%3d", clampi(p.tilt_y, -99, 99));
                display::text(x + 62, y + 44, text, 2, col(Role::Fg));
            } else {
                display::text(x + 62, y + 34, "...", 2, col(Role::Dim));
            }
            break;
        }
        case PortKind::Motion: {
            label_right(right, y + 6, "MOTION", Role::Accent);
            const int track_w = kPortW - 2 * kPad;
            display::round_rect(x + kPad, y + 54, track_w, 8, 4, pal.track);
            if (p.has_value) {
                const int v = clampi(p.level, 0, 100);
                std::snprintf(text, sizeof text, "%d", v);
                display::text(x + kPad, y + 22, text, 3, col(Role::Fg));
                const int w = track_w * v / 100;
                if (w >= 8) display::round_rect(x + kPad, y + 54, w, 8, 4, col(Role::Accent));
            } else {
                display::text(x + kPad, y + 26, "...", 2, col(Role::Dim));
            }
            break;
        }
        default: {
            label_right(right, y + 6, "UNKNOWN", Role::Warn);
            std::snprintf(text, sizeof text, "TYPE %u", p.type_id);
            display::text(x + kPad, y + 34, text, 2, col(Role::Warn));
            break;
        }
    }
    display::present(x, y, kPortW, kMainH);
}

uint64_t sig_port(unsigned index, const Port &p) {
    uint64_t s = 1;   // never 0: 0 means "nothing shown"
    s = s * 257u + static_cast<uint8_t>(p.kind);
    s = s * 257u + p.type_id;
    s = s * 257u + (p.has_value ? 1u : 0u);
    s = s * 257u + static_cast<uint8_t>(clampi(p.tilt_x, -99, 99) + 100);
    s = s * 257u + static_cast<uint8_t>(clampi(p.tilt_y, -99, 99) + 100);
    s = s * 257u + static_cast<uint8_t>(clampi(p.level, 0, 100));
    s = s * 257u + static_cast<uint8_t>(clampi(p.power, -100, 100) + 100);
    s = s * 1009u + static_cast<uint32_t>(p.kind == PortKind::Motor ? g_angle[index] : 0);
    return s;
}

// --- wide card while there is no link --------------------------------------------
void draw_wide(const Model &m) {
    const int x = kMargin;
    const int y = kMainY;
    card(x, y, kWideW, kMainH);
    const int cx = x + 38;
    const int cy = y + kMainH / 2;
    const int head = static_cast<int>(g_frame / 2) % kSpinDots;
    for (int k = 0; k < kSpinDots; k++) {
        const float a = static_cast<float>(k) * 2.0f * kPi / static_cast<float>(kSpinDots);
        const int px = cx + static_cast<int>(std::lround(16.0f * std::cos(a)));
        const int py = cy + static_cast<int>(std::lround(16.0f * std::sin(a)));
        const int behind = (head - k + kSpinDots) % kSpinDots;   // 0 = head, 1 and 2 = tail
        if (behind == 0) {
            display::fill_circle(px, py, 4, col(Role::Warn));
        } else if (behind <= 2) {
            display::fill_circle(px, py, 4 - behind, col(Role::Warn));
        } else {
            display::fill_circle(px, py, 2, theme::palette().edge);
        }
    }
    const char *line1 = "Press green";
    const char *line2 = "hub button";
    if (m.link == Link::Connecting) {
        line1 = "Connecting";
        line2 = "to the hub";
    } else if (m.link == Link::Setup) {
        line1 = "Setting up";
        line2 = "the link";
    }
    display::text(x + 72, y + 17, line1, 2, col(Role::Fg));
    display::text(x + 72, y + 39, line2, 2, col(Role::Fg));
    display::present(x, y, kWideW, kMainH);
}

// --- bottom row ----------------------------------------------------------------
void draw_power(const Model &m) {
    const theme::Palette &pal = theme::palette();
    const int x = kPowerX;
    const int y = kLowY;
    card(x, y, kPowerW, kLowH);
    if (m.locked) {
        label(x + kPad, y + 5, "LOCKED", Role::Warn);
    } else {
        label(x + kPad, y + 5, "POWER");
    }
    char text[8];
    std::snprintf(text, sizeof text, "%d%%", clampi(m.power, 0, 100));
    display::text(x + kPad, y + 16, text, 2, col(Role::Fg));
    const int tx = x + kPowerW - 13;
    const int th = 26;
    display::round_rect(tx, y + 5, 6, th, 3, pal.track);
    int fh = th * clampi(m.power, 0, 100) / 100;
    if (m.power > 0 && fh < 6) fh = 6;   // keep the lowest setting visible
    if (fh >= 6) display::round_rect(tx, y + 5 + th - fh, 6, fh, 3, col(m.drive != 0 ? Role::Good : Role::Accent));
    display::present(x, y, kPowerW, kLowH);
}

void draw_led(const Model &m) {
    const int x = kLedX;
    const int y = kLowY;
    card(x, y, kLedW, kLowH);
    label(x + kPad, y + 5, "LED");
    const theme::LedPreset &led = theme::led_preset(m.led);
    const int cx = x + 17;
    const int cy = y + 24;
    if (m.led == 0) {
        display::ring(cx, cy, 7, 5, led.swatch);
    } else {
        display::fill_circle(cx, cy, 7, led.swatch);
    }
    display::text(x + 30, y + 21, led.name, 1, col(m.led == 0 ? Role::Dim : Role::Fg));
    display::present(x, y, kLedW, kLowH);
}

int tone_bar(int i) {
    // Deterministic "equalizer": different period per bar.
    return 4 + static_cast<int>((g_frame * static_cast<uint32_t>(3 + 2 * i) + static_cast<uint32_t>(5 * i)) % 13u);
}

void draw_tone(const Model &m) {
    const int x = kToneX;
    const int y = kLowY;
    card(x, y, kToneW, kLowH);
    label(x + kPad, y + 5, "TONE", m.tone_playing ? Role::Accent : Role::Dim);
    if (m.note != nullptr) {
        display::text(x + kPad, y + 16, m.note, 2, col(Role::Fg));
    } else {
        display::text(x + kPad, y + 16, "--", 2, col(Role::Dim));
    }
    for (int i = 0; i < kToneBars; i++) {
        const int h = m.tone_playing ? tone_bar(i) : 2;
        display::fill_rect(x + 42 + i * 7, y + 32 - h, 4, h, m.tone_playing ? col(Role::Accent) : theme::palette().edge);
    }
    display::present(x, y, kToneW, kLowH);
}
}  // namespace

void reset() {
    g_shown_bar = 0;
    g_shown_main[0] = 0;
    g_shown_main[1] = 0;
    g_shown_wide = 0;
    g_shown_power = 0;
    g_shown_led = 0;
    g_shown_tone = 0;
    g_shown_mode = -1;
}

void step(const Model &m) {
    g_frame++;
    for (unsigned i = 0; i < 2; i++) {
        const Port &p = m.port[i];
        if (p.kind != PortKind::Motor || p.power == 0) continue;
        const int mag = p.power > 0 ? p.power : -p.power;
        const int deg = 8 + 22 * mag / 100;          // per frame; below 60 so 3 spokes never appear to reverse
        g_angle[i] = (g_angle[i] + (p.power > 0 ? deg : 360 - deg)) % 360;
    }
}

void render(const Model &m) {
    const bool ready = m.link == Link::Ready;
    const bool busy = !ready && m.link != Link::Idle;

    const uint64_t bar = 1u + static_cast<uint64_t>(m.link) * 16u + (busy ? (g_frame / 4) % 3 : 0u);
    if (bar != g_shown_bar) {
        draw_bar(m);
        display::present(0, 0, display::kWidth, kBarH);
        g_shown_bar = bar;
    }

    const int mode = ready ? 1 : 0;
    if (mode != g_shown_mode) {
        display::fill_rect(0, kMainY, display::kWidth, kMainH, theme::palette().bg);
        display::present(0, kMainY, display::kWidth, kMainH);
        g_shown_mode = mode;
        g_shown_main[0] = 0;
        g_shown_main[1] = 0;
        g_shown_wide = 0;
    }
    if (ready) {
        for (unsigned i = 0; i < 2; i++) {
            const uint64_t s = sig_port(i, m.port[i]);
            if (s == g_shown_main[i]) continue;
            draw_port(i, m.port[i]);
            g_shown_main[i] = s;
        }
    } else {
        const uint64_t s = 1u + static_cast<uint64_t>(m.link) * 16u + (g_frame / 2) % kSpinDots;
        if (s != g_shown_wide) {
            draw_wide(m);
            g_shown_wide = s;
        }
    }

    const uint64_t power = 1u + static_cast<uint64_t>(clampi(m.power, 0, 100)) * 8u + (m.locked ? 4u : 0u) +
                           (m.drive != 0 ? 2u : 0u);
    if (power != g_shown_power) {
        draw_power(m);
        g_shown_power = power;
    }

    const uint64_t led = 1u + m.led;
    if (led != g_shown_led) {
        draw_led(m);
        g_shown_led = led;
    }

    uint64_t tone = 1u;
    for (const char *c = m.note; c != nullptr && *c != '\0'; c++) tone = tone * 131u + static_cast<uint8_t>(*c);
    tone *= 64u;
    if (m.tone_playing) tone += 1u + (g_frame % 32u);
    if (tone != g_shown_tone) {
        draw_tone(m);
        g_shown_tone = tone;
    }
}

}  // namespace screen
