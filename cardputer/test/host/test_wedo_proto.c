// SPDX-License-Identifier: AGPL-3.0-or-later
// test_wedo_proto.c - host test of components/app_wedo/wedo_proto.c.
//
// The expected bytes are the ones wedo2.py sent to the real hub.
// Build and run from the project root (one line):
//   cc -std=c11 -Wall -Wextra -Wpedantic -Werror -I components/app_wedo/include
//      test/host/test_wedo_proto.c components/app_wedo/wedo_proto.c -o test_wedo
//   ./test_wedo

#include <stdio.h>
#include <string.h>

#include "wedo_proto.h"

static int g_fail = 0;

static void expect_hex(const char *name, const uint8_t *got, size_t len, const char *want)
{
    char text[2u * WEDO_CMD_MAX + 1u] = {0};
    for (size_t i = 0; i < len && i < WEDO_CMD_MAX; i++) {
        snprintf(&text[2u * i], 3u, "%02x", got[i]);
    }
    if (strcmp(text, want) != 0) {
        printf("FAIL %s: got %s want %s\n", name, text, want);
        g_fail++;
    } else {
        printf("ok   %s\n", name);
    }
}

static void expect_int(const char *name, int got, int want)
{
    if (got != want) {
        printf("FAIL %s: got %d want %d\n", name, got, want);
        g_fail++;
    } else {
        printf("ok   %s\n", name);
    }
}

int main(void)
{
    uint8_t b[WEDO_CMD_MAX];
    size_t n;

    n = wedo_cmd_motor(b, 1u, 100);
    expect_hex("motor 1 +100", b, n, "01010164");
    n = wedo_cmd_motor(b, 2u, -100);
    expect_hex("motor 2 -100", b, n, "0201019c");
    n = wedo_cmd_motor(b, 1u, 0);
    expect_hex("motor 1 stop", b, n, "01010100");
    n = wedo_cmd_motor(b, 1u, 60);
    expect_hex("motor 1 +60", b, n, "01010148");
    expect_int("motor byte 50", wedo_motor_byte(50), 65);
    expect_int("motor byte 1", wedo_motor_byte(1), 31);
    expect_int("motor byte 999 clamps", wedo_motor_byte(999), 100);
    expect_int("motor byte -999 clamps", wedo_motor_byte(-999), -100);

    n = wedo_cmd_led(b, 0xFFu, 0u, 0u);
    expect_hex("led red", b, n, "060403ff0000");
    n = wedo_cmd_tone(b, 440u, 500u);
    expect_hex("tone 440 500", b, n, "050204b801f401");
    n = wedo_cmd_tone_stop(b);
    expect_hex("tone stop", b, n, "0503");
    n = wedo_cmd_led_mode_rgb(b);
    expect_hex("led mode rgb", b, n, "0102061701000000000000");
    n = wedo_cmd_sensor_format(b, 1u, WEDO_TYPE_MOTION);
    expect_hex("motion format", b, n, "0102012300010000000101");
    n = wedo_cmd_sensor_format(b, 2u, WEDO_TYPE_TILT);
    expect_hex("tilt format", b, n, "0102022200010000000001");
    expect_int("motor has no format", (int)wedo_cmd_sensor_format(b, 1u, WEDO_TYPE_MOTOR), 0);

    wedo_attach_t a;
    const uint8_t att_motor[] = {2u, 1u, 0u, 1u, 0u, 0u};
    expect_int("attach ok", wedo_parse_attach(att_motor, sizeof att_motor, &a), 1);
    expect_int("attach port", a.port, 2);
    expect_int("attach type", a.type, WEDO_TYPE_MOTOR);
    expect_int("attach flag", a.attached, 1);
    const uint8_t det[] = {2u, 0u};
    expect_int("detach ok", wedo_parse_attach(det, sizeof det, &a), 1);
    expect_int("detach flag", a.attached, 0);
    expect_int("detach type", a.type, WEDO_TYPE_NONE);
    const uint8_t short_att[] = {2u, 1u, 0u};
    expect_int("attach too short", wedo_parse_attach(short_att, sizeof short_att, &a), 0);
    expect_int("attach empty", wedo_parse_attach(short_att, 0u, &a), 0);

    wedo_value_t v;
    const uint8_t tilt[] = {5u, 1u, 0xF4u, 0x03u};
    expect_int("value ok", wedo_parse_value(tilt, sizeof tilt, &v), 1);
    expect_int("value port", v.port, 1);
    expect_int("value x", (int8_t)v.raw[0], -12);
    expect_int("value y", (int8_t)v.raw[1], 3);
    const uint8_t motion[] = {5u, 2u, 87u};
    expect_int("motion ok", wedo_parse_value(motion, sizeof motion, &v), 1);
    expect_int("motion value", v.raw[0], 87);
    expect_int("value too short", wedo_parse_value(motion, 2u, &v), 0);

    printf(g_fail == 0 ? "ALL PASS\n" : "%d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
