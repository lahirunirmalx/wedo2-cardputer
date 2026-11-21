// SPDX-License-Identifier: AGPL-3.0-or-later
// wedo_hub.hpp - one WeDo 2.0 hub session on top of hal_ble_central.
//
// Not thread-safe: one task owns the Hub and feeds it the BLE events it
// took off its queue (on_state / on_notify) and a regular tick().
//
// Everything sent to the hub is "wanted state": tick() compares it with
// what was last sent and writes the difference, so a write that fails
// (link busy) is retried on the next tick and nothing is sent twice.

#pragma once

#include <cstddef>
#include <cstdint>

#include "hal_ble_central.h"
#include "wedo_proto.h"

namespace wedo {

enum Chr : uint8_t {
    kChrAttachedIo = 0,
    kChrLowVoltage,
    kChrInputValues,
    kChrInputCommand,
    kChrOutputCommand,
    kChrCount
};

constexpr int kPowerMin = 10;
constexpr int kPowerMax = WEDO_POWER_MAX;
constexpr int kPowerStep = 10;
constexpr int kPowerDefault = 60;

struct PortView {
    uint8_t type;        // WEDO_TYPE_*
    bool has_value;      // a sensor value has arrived
    uint8_t raw[2];      // see wedo_value_t
    int power;           // motor: last power sent, -100..100
};

class Hub {
public:
    /** Service, characteristics and notify mask for hal_ble_central_start(). Callbacks stay the caller's. */
    static void fill_config(hal_ble_central_cfg_t *cfg);

    void on_state(hal_ble_state_t state, uint32_t now_ms);
    void on_notify(uint8_t chr, const uint8_t *data, size_t len, uint32_t now_ms);
    void tick(uint32_t now_ms);

    /** +1 forward, -1 reverse, 0 stop. Applies to every port that holds a motor. */
    void set_drive(int dir);
    /** Clamped to kPowerMin..kPowerMax. */
    void set_power(int power);
    void set_led(uint8_t r, uint8_t g, uint8_t b);
    void play_tone(uint16_t hz, uint16_t ms);
    /** Motors off and buzzer silent. */
    void stop_all();

    hal_ble_state_t state() const { return state_; }
    int power() const { return power_; }
    int drive() const { return drive_; }
    /** @p index 0 = port 1, 1 = port 2. */
    const PortView &port(unsigned index) const { return ports_[index < WEDO_EXT_PORTS ? index : 0]; }

    /** True once after something a screen would show has changed. */
    bool take_dirty();

private:
    bool send(uint8_t chr, const uint8_t *data, size_t len);
    void clear_ports();

    hal_ble_state_t state_ = HAL_BLE_IDLE;
    PortView ports_[WEDO_EXT_PORTS] = {};
    bool format_pending_[WEDO_EXT_PORTS] = {};
    uint32_t value_log_ms_[WEDO_EXT_PORTS] = {};
    int drive_ = 0;
    int power_ = kPowerDefault;
    uint8_t led_[3] = {0, 0, 0xFF};
    bool led_mode_pending_ = false;
    bool led_pending_ = false;
    uint16_t tone_hz_ = 0;
    uint16_t tone_ms_ = 0;
    bool tone_pending_ = false;
    bool tone_stop_pending_ = false;
    uint32_t keepalive_ms_ = 0;
    uint32_t scan_try_ms_ = 0;
    bool scan_tried_ = false;
    bool dirty_ = true;
};

}  // namespace wedo
