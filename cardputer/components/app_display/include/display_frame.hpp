// SPDX-License-Identifier: AGPL-3.0-or-later
// display_frame.hpp - read access to the off-screen frame, for present()
// and for the host preview. Not for screens: they use display.hpp.

#pragma once

#include <cstddef>
#include <cstdint>

#include "display.hpp"

namespace display {
namespace detail {

constexpr size_t kStride = static_cast<size_t>(kWidth) * 2;   // bytes per row

/** kHeight rows of kStride bytes: 2 bytes per pixel, high byte first. */
const uint8_t *frame();

}  // namespace detail
}  // namespace display
