// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 lahiru
// font5x7.h - classic 5x7 font, ASCII 0x20..0x7E, 5 column bytes per glyph,
// bit 0 = top. Characters outside that range draw as a space.

#ifndef APP_DISPLAY_FONT5X7_H
#define APP_DISPLAY_FONT5X7_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *font5x7_glyph(char c);

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_FONT5X7_H */
