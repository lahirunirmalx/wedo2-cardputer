// SPDX-License-Identifier: AGPL-3.0-or-later
// keyboard.hpp - Cardputer 56-key matrix: scan, debounce, press / release
// events. Printable keys report their unshifted ASCII character.

#pragma once

#include <cstdint>

namespace kbd {

enum Key : uint8_t {
    kNone = 0,
    kBackspace = 0x08,
    kTab = '\t',
    kEnter = '\n',
    kFn = 0x80,
    kShift,
    kCtrl,
    kOpt,
    kAlt,
};

struct Event {
    uint8_t key;   // Key value or an ASCII character
    bool down;     // true = pressed, false = released
};

using Sink = void (*)(const Event &event, void *user);

/** Configure the matrix pins. */
bool init();

/**
 * Sample the matrix once and report changes. Call every 10 ms: a change is
 * reported when two samples in a row agree.
 */
void scan(Sink sink, void *user);

}  // namespace kbd
