// SPDX-License-Identifier: AGPL-3.0-or-later
// screen.hpp - the card-style screen of the WeDo controller.
//
// Layout (240 x 135):
//   top bar      title and a link status pill
//   main area    one card per hub port (motor with a spinning rotor, tilt
//                with a level bubble, motion with a gauge) or, while there
//                is no link, one wide card with a spinner
//   bottom row   POWER, LED and TONE cards
//
// Portable C++: draws only through display.hpp and takes plain data, so
// test/host/render_preview.cpp renders it on a PC.
// Only the cards whose content changed are redrawn and sent to the panel.

#pragma once

#include <cstdint>

namespace screen {

enum class Link : uint8_t { Idle, Scanning, Connecting, Setup, Ready };

enum class PortKind : uint8_t { Empty, Motor, Tilt, Motion, Unknown };

struct Port {
    PortKind kind = PortKind::Empty;
    uint8_t type_id = 0;       // raw device type, shown for Unknown
    bool has_value = false;    // Tilt / Motion: a reading has arrived
    int tilt_x = 0;            // Tilt
    int tilt_y = 0;
    int level = 0;             // Motion: 0..100
    int power = 0;             // Motor: -100..100 as last sent, 0 = stopped
};

struct Model {
    Link link = Link::Idle;
    Port port[2];
    int power = 0;             // selected power, 10..100
    int drive = 0;             // +1 forward, -1 reverse, 0 stop
    bool locked = false;       // continuous run is on
    unsigned led = 0;          // theme LED preset index
    const char *note = nullptr;   // last note name, nullptr = none yet
    bool tone_playing = false;
};

constexpr uint32_t kFrameMs = 50;   // call step() at this period

/** Forget what is on the panel: the next render() redraws everything. */
void reset();

/** Advance the animations by one frame. */
void step(const Model &m);

/** Redraw and present whatever differs from what is on the panel. */
void render(const Model &m);

}  // namespace screen
