// SPDX-License-Identifier: AGPL-3.0-or-later
// hal_clock.h - short blocking delay for bus settle times.
//
// Only for microsecond-scale waits where a scheduler delay is too coarse
// (keyboard matrix settle). Longer waits use the RTOS delay.

#pragma once

#include <stdint.h>

#include "hal_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_CLOCK_MAX_DELAY_US 1000u

/** Busy-wait @p us microseconds. HAL_ERR_INVALID_ARG above HAL_CLOCK_MAX_DELAY_US. */
hal_result_t hal_clock_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif
