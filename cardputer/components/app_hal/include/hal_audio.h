// SPDX-License-Identifier: AGPL-3.0-or-later
// hal_audio.h - a single-voice tone generator on a small I2S speaker amp.
//
// hal_audio_tone() and hal_audio_stop() return at once; a task inside the
// port produces the samples. A new tone replaces the one playing. Starts
// and stops are faded over a few milliseconds so they do not click.
//
// Device implementation: components/port_esp_idf/port_audio.c.

#pragma once

#include <stdint.h>

#include "hal_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_AUDIO_VOLUME_MAX 100u

typedef struct {
    int16_t  bclk_pin;
    int16_t  ws_pin;
    int16_t  dout_pin;
    uint32_t sample_rate;   /**< e.g. 16000 */
} hal_audio_cfg_t;

/** Bring up the I2S output and the tone task. HAL_ERR_BUSY if already done. */
hal_result_t hal_audio_init(const hal_audio_cfg_t *cfg);

/**
 * Play a sine tone. @p ms = 0 plays until hal_audio_stop() or the next tone.
 * @p volume is 0..HAL_AUDIO_VOLUME_MAX (clamped). HAL_ERR_INVALID_ARG for a
 * frequency of 0 or above a quarter of the sample rate.
 */
hal_result_t hal_audio_tone(uint16_t hz, uint16_t ms, uint8_t volume);

/** Fade the current tone out. No-op when silent. */
hal_result_t hal_audio_stop(void);

#ifdef __cplusplus
}
#endif
