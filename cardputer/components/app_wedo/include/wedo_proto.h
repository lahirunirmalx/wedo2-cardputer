// SPDX-License-Identifier: AGPL-3.0-or-later
// wedo_proto.h - LEGO WeDo 2.0 Smart Hub wire format, portable C11.
//
// Source: scratch-vm, src/extensions/scratch3_wedo2/index.js. The command
// bytes match ../../../wedo2.py, which was run against the real hub.
// No HAL or ESP-IDF header here: host-tested by test/host/test_wedo_proto.c.

#ifndef APP_WEDO_PROTO_H
#define APP_WEDO_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEDO_CMD_MAX       11u   /**< longest command in bytes */
#define WEDO_EXT_PORTS     2u    /**< external ports: 1 and 2 */

#define WEDO_PORT_PIEZO    5u
#define WEDO_PORT_LED      6u

#define WEDO_TYPE_NONE     0u
#define WEDO_TYPE_MOTOR    1u
#define WEDO_TYPE_VOLTAGE  20u
#define WEDO_TYPE_CURRENT  21u
#define WEDO_TYPE_PIEZO    22u
#define WEDO_TYPE_LED      23u
#define WEDO_TYPE_TILT     34u
#define WEDO_TYPE_MOTION   35u

#define WEDO_POWER_MAX     100

/* UUIDs as written: xxxxxxxx-1212-efde-1523-785feabcd123 */
#define WEDO_UUID(a, b, c, d)                                                                          \
    {                                                                                                  \
        {                                                                                              \
            (a), (b), (c), (d), 0x12, 0x12, 0xef, 0xde, 0x15, 0x23, 0x78, 0x5f, 0xea, 0xbc, 0xd1, 0x23 \
        }                                                                                              \
    }
#define WEDO_UUID_DEVICE_SERVICE     WEDO_UUID(0x00, 0x00, 0x15, 0x23)
#define WEDO_UUID_ATTACHED_IO        WEDO_UUID(0x00, 0x00, 0x15, 0x27)
#define WEDO_UUID_LOW_VOLTAGE_ALERT  WEDO_UUID(0x00, 0x00, 0x15, 0x28)
#define WEDO_UUID_INPUT_VALUES       WEDO_UUID(0x00, 0x00, 0x15, 0x60)
#define WEDO_UUID_INPUT_COMMAND      WEDO_UUID(0x00, 0x00, 0x15, 0x63)
#define WEDO_UUID_OUTPUT_COMMAND     WEDO_UUID(0x00, 0x00, 0x15, 0x65)

/**
 * Power byte for a motor command. @p power is -100..100 (clamped); the hub
 * only turns a motor from about 30 up, so 1..100 maps onto 30..100.
 */
int8_t wedo_motor_byte(int power);

/* Each builder writes at most WEDO_CMD_MAX bytes and returns the length. */

/** OUTPUT_COMMAND: motor on @p port at @p power (-100..100, 0 = stop). */
size_t wedo_cmd_motor(uint8_t *out, uint8_t port, int power);

/** OUTPUT_COMMAND: hub LED colour. Needs wedo_cmd_led_mode_rgb() once per connection. */
size_t wedo_cmd_led(uint8_t *out, uint8_t r, uint8_t g, uint8_t b);

/** OUTPUT_COMMAND: buzzer tone. */
size_t wedo_cmd_tone(uint8_t *out, uint16_t hz, uint16_t ms);

/** OUTPUT_COMMAND: stop the buzzer. */
size_t wedo_cmd_tone_stop(uint8_t *out);

/** INPUT_COMMAND: put the hub LED into RGB mode. */
size_t wedo_cmd_led_mode_rgb(uint8_t *out);

/**
 * INPUT_COMMAND: start value notifications from a tilt or motion sensor.
 * Returns 0 for any other type.
 */
size_t wedo_cmd_sensor_format(uint8_t *out, uint8_t port, uint8_t type);

typedef struct {
    uint8_t port;
    bool    attached;
    uint8_t type;      /**< WEDO_TYPE_*; WEDO_TYPE_NONE when detached */
} wedo_attach_t;

/** Decode an ATTACHED_IO notification. False when too short. */
bool wedo_parse_attach(const uint8_t *data, size_t len, wedo_attach_t *out);

typedef struct {
    uint8_t port;
    uint8_t raw[2];    /**< tilt: x, y as signed bytes; motion: raw[0] = 0..100 */
} wedo_value_t;

/** Decode an INPUT_VALUES notification. False when too short. */
bool wedo_parse_value(const uint8_t *data, size_t len, wedo_value_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_WEDO_PROTO_H */
