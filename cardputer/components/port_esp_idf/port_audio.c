// SPDX-License-Identifier: AGPL-3.0-or-later
// port_audio.c - ESP-IDF implementation of hal_audio.h.
//
// I2S standard mode, 16 bit, both slots carry the same sample (the amp picks
// one). A task pinned to core 1 writes blocks while a tone is playing and
// blocks on its request queue while silent; the channel then sends zeros by
// itself (auto_clear). C, not C++: the I2S config macros are C initializers.

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "hal_audio.h"

#define TAG "port_audio"

#define AUDIO_I2S_PORT     I2S_NUM_1
#define AUDIO_TASK_CORE    1
#define AUDIO_TASK_PRIO    (tskIDLE_PRIORITY + 6)
#define AUDIO_TASK_STACK   3072
#define BLOCK_FRAMES       256
#define SINE_BITS          10
#define SINE_SIZE          (1 << SINE_BITS)
#define ENV_MAX            256          /* envelope full scale */
#define FADE_MS            6
#define PEAK_AT_FULL       26000        /* sample peak at volume 100 (of 32767) */

typedef struct {
    uint16_t hz;      /* 0 = stop */
    uint16_t ms;      /* 0 = until stopped */
    uint8_t  volume;
} request_t;

static i2s_chan_handle_t s_tx;
static QueueHandle_t s_queue;
static uint32_t s_rate;
static int16_t s_sine[SINE_SIZE];
static int16_t s_block[BLOCK_FRAMES * 2];

static hal_result_t map_err(esp_err_t e)
{
    switch (e) {
        case ESP_OK:                return HAL_OK;
        case ESP_ERR_INVALID_ARG:   return HAL_ERR_INVALID_ARG;
        case ESP_ERR_NO_MEM:        return HAL_ERR_NO_MEM;
        case ESP_ERR_TIMEOUT:       return HAL_ERR_TIMEOUT;
        case ESP_ERR_NOT_FOUND:     return HAL_ERR_NOT_FOUND;
        case ESP_ERR_INVALID_STATE: return HAL_ERR_BUSY;
        case ESP_ERR_NOT_SUPPORTED: return HAL_ERR_UNSUPPORTED;
        default:                    return HAL_ERR_INTERNAL;
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    uint32_t phase = 0;        /* 32-bit phase accumulator */
    uint32_t step = 0;         /* phase increment per sample */
    int32_t peak = 0;          /* sample peak for the current tone */
    int32_t env = 0;           /* 0..ENV_MAX */
    int32_t env_target = 0;
    uint32_t frames_left = 0;  /* 0 with timed == false: no limit */
    bool timed = false;
    int32_t env_step = (int32_t)(((int64_t)ENV_MAX * 1000) / ((int64_t)s_rate * FADE_MS));
    if (env_step < 1) {
        env_step = 1;
    }

    for (;;) {
        request_t r;
        const bool silent = (env == 0 && env_target == 0);
        if (xQueueReceive(s_queue, &r, silent ? portMAX_DELAY : 0) == pdTRUE) {
            if (r.hz == 0) {
                env_target = 0;
            } else {
                step = (uint32_t)(((uint64_t)r.hz << 32) / s_rate);
                peak = (int32_t)PEAK_AT_FULL * r.volume / (int32_t)HAL_AUDIO_VOLUME_MAX;
                env_target = ENV_MAX;
                timed = (r.ms != 0);
                frames_left = (uint32_t)(((uint64_t)r.ms * s_rate) / 1000u);
            }
        }
        for (int i = 0; i < BLOCK_FRAMES; i++) {
            if (timed && env_target != 0) {
                if (frames_left == 0) {
                    env_target = 0;
                } else {
                    frames_left--;
                }
            }
            if (env < env_target) {
                env += env_step;
                if (env > env_target) {
                    env = env_target;
                }
            } else if (env > env_target) {
                env -= env_step;
                if (env < env_target) {
                    env = env_target;
                }
            }
            const int32_t s = (int32_t)s_sine[phase >> (32 - SINE_BITS)];   /* -32767..32767 */
            const int16_t v = (int16_t)((((s * peak) >> 15) * env) / ENV_MAX);
            phase += step;
            s_block[2 * i] = v;
            s_block[2 * i + 1] = v;
        }
        size_t written = 0;
        const esp_err_t e = i2s_channel_write(s_tx, s_block, sizeof s_block, &written, pdMS_TO_TICKS(200));
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "i2s write: %s", esp_err_to_name(e));
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

hal_result_t hal_audio_init(const hal_audio_cfg_t *cfg)
{
    if (s_tx != NULL) {
        return HAL_ERR_BUSY;
    }
    if (cfg == NULL || cfg->bclk_pin < 0 || cfg->ws_pin < 0 || cfg->dout_pin < 0 || cfg->sample_rate < 8000u ||
        cfg->sample_rate > 48000u) {
        return HAL_ERR_INVALID_ARG;
    }
    s_rate = cfg->sample_rate;
    for (int i = 0; i < SINE_SIZE; i++) {
        s_sine[i] = (int16_t)lroundf(32767.0f * sinf(2.0f * (float)M_PI * (float)i / (float)SINE_SIZE));
    }

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan.auto_clear = true;   /* silence, not a repeated last block, when the task is idle */
    esp_err_t e = i2s_new_channel(&chan, &s_tx, NULL);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(e));
        s_tx = NULL;
        return map_err(e);
    }
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = (gpio_num_t)cfg->bclk_pin,
                .ws = (gpio_num_t)cfg->ws_pin,
                .dout = (gpio_num_t)cfg->dout_pin,
                .din = I2S_GPIO_UNUSED,
            },
    };
    e = i2s_channel_init_std_mode(s_tx, &std);
    if (e == ESP_OK) {
        e = i2s_channel_enable(s_tx);
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s setup: %s", esp_err_to_name(e));
        (void)i2s_del_channel(s_tx);
        s_tx = NULL;
        return map_err(e);
    }

    s_queue = xQueueCreate(1, sizeof(request_t));
    if (s_queue == NULL ||
        xTaskCreatePinnedToCore(audio_task, "audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL, AUDIO_TASK_CORE) !=
            pdPASS) {
        ESP_LOGE(TAG, "no memory for the audio task");
        return HAL_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "speaker ok: %u Hz, bclk=GPIO%d ws=GPIO%d dout=GPIO%d", (unsigned)s_rate, cfg->bclk_pin, cfg->ws_pin,
             cfg->dout_pin);
    return HAL_OK;
}

hal_result_t hal_audio_tone(uint16_t hz, uint16_t ms, uint8_t volume)
{
    if (s_queue == NULL) {
        return HAL_ERR_BUSY;
    }
    if (hz == 0 || (uint32_t)hz * 4u > s_rate) {
        return HAL_ERR_INVALID_ARG;
    }
    const request_t r = {hz, ms, volume > HAL_AUDIO_VOLUME_MAX ? (uint8_t)HAL_AUDIO_VOLUME_MAX : volume};
    (void)xQueueOverwrite(s_queue, &r);
    return HAL_OK;
}

hal_result_t hal_audio_stop(void)
{
    if (s_queue == NULL) {
        return HAL_ERR_BUSY;
    }
    const request_t r = {0, 0, 0};
    (void)xQueueOverwrite(s_queue, &r);
    return HAL_OK;
}
