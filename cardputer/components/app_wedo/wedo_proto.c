// SPDX-License-Identifier: AGPL-3.0-or-later
// wedo_proto.c - see wedo_proto.h.

#include "wedo_proto.h"

#define CMD_MOTOR_POWER 1u
#define CMD_PLAY_TONE   2u
#define CMD_STOP_TONE   3u
#define CMD_WRITE_RGB   4u

#define MODE_LED_RGB    1u
#define UNIT_RAW        0u
#define UNIT_PERCENT    1u

#define MOTOR_MIN_BYTE  30

int8_t wedo_motor_byte(int power)
{
    if (power > WEDO_POWER_MAX) {
        power = WEDO_POWER_MAX;
    }
    if (power < -WEDO_POWER_MAX) {
        power = -WEDO_POWER_MAX;
    }
    if (power == 0) {
        return 0;
    }
    const int mag = power < 0 ? -power : power;
    const int scaled = MOTOR_MIN_BYTE + ((WEDO_POWER_MAX - MOTOR_MIN_BYTE) * mag + WEDO_POWER_MAX / 2) / WEDO_POWER_MAX;
    return (int8_t)(power < 0 ? -scaled : scaled);
}

size_t wedo_cmd_motor(uint8_t *out, uint8_t port, int power)
{
    out[0] = port;
    out[1] = CMD_MOTOR_POWER;
    out[2] = 1u;
    out[3] = (uint8_t)wedo_motor_byte(power);
    return 4u;
}

size_t wedo_cmd_led(uint8_t *out, uint8_t r, uint8_t g, uint8_t b)
{
    out[0] = WEDO_PORT_LED;
    out[1] = CMD_WRITE_RGB;
    out[2] = 3u;
    out[3] = r;
    out[4] = g;
    out[5] = b;
    return 6u;
}

size_t wedo_cmd_tone(uint8_t *out, uint16_t hz, uint16_t ms)
{
    out[0] = WEDO_PORT_PIEZO;
    out[1] = CMD_PLAY_TONE;
    out[2] = 4u;
    out[3] = (uint8_t)(hz & 0xFFu);
    out[4] = (uint8_t)(hz >> 8);
    out[5] = (uint8_t)(ms & 0xFFu);
    out[6] = (uint8_t)(ms >> 8);
    return 7u;
}

size_t wedo_cmd_tone_stop(uint8_t *out)
{
    out[0] = WEDO_PORT_PIEZO;
    out[1] = CMD_STOP_TONE;
    return 2u;
}

/* (command 1 = sensor format, type 2 = write, port, type, mode, delta x4, unit, notify) */
static size_t input_command(uint8_t *out, uint8_t port, uint8_t type, uint8_t mode, uint8_t delta, uint8_t unit,
                            bool notify)
{
    out[0] = 1u;
    out[1] = 2u;
    out[2] = port;
    out[3] = type;
    out[4] = mode;
    out[5] = delta;
    out[6] = 0u;
    out[7] = 0u;
    out[8] = 0u;
    out[9] = unit;
    out[10] = notify ? 1u : 0u;
    return 11u;
}

size_t wedo_cmd_led_mode_rgb(uint8_t *out)
{
    return input_command(out, WEDO_PORT_LED, WEDO_TYPE_LED, MODE_LED_RGB, 0u, UNIT_RAW, false);
}

size_t wedo_cmd_sensor_format(uint8_t *out, uint8_t port, uint8_t type)
{
    if (type == WEDO_TYPE_TILT) {
        return input_command(out, port, type, 0u, 1u, UNIT_RAW, true);
    }
    if (type == WEDO_TYPE_MOTION) {
        return input_command(out, port, type, 0u, 1u, UNIT_PERCENT, true);
    }
    return 0u;
}

bool wedo_parse_attach(const uint8_t *data, size_t len, wedo_attach_t *out)
{
    if (data == NULL || out == NULL || len < 2u) {
        return false;
    }
    out->port = data[0];
    if (data[1] == 0u) {
        out->attached = false;
        out->type = WEDO_TYPE_NONE;
        return true;
    }
    if (len < 4u) {
        return false;
    }
    out->attached = true;
    out->type = data[3];
    return true;
}

bool wedo_parse_value(const uint8_t *data, size_t len, wedo_value_t *out)
{
    if (data == NULL || out == NULL || len < 3u) {
        return false;
    }
    out->port = data[1];
    out->raw[0] = data[2];
    out->raw[1] = len >= 4u ? data[3] : 0u;
    return true;
}
