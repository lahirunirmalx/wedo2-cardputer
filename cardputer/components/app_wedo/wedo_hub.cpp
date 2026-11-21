// SPDX-License-Identifier: AGPL-3.0-or-later
// wedo_hub.cpp - see wedo_hub.hpp.

#include "wedo_hub.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

namespace wedo {
namespace {
constexpr char TAG[] = "wedo";
constexpr uint32_t kScanRetryMs = 1000;
constexpr uint32_t kKeepaliveMs = 5000;   // scratch-vm polls the hub at this rate to keep the link up
constexpr uint32_t kValueLogMs = 250;     // at most one sensor log line per port in this time

constexpr hal_ble_uuid128_t kService = WEDO_UUID_DEVICE_SERVICE;
constexpr hal_ble_uuid128_t kChrs[kChrCount] = {
    WEDO_UUID_ATTACHED_IO,   WEDO_UUID_LOW_VOLTAGE_ALERT, WEDO_UUID_INPUT_VALUES,
    WEDO_UUID_INPUT_COMMAND, WEDO_UUID_OUTPUT_COMMAND,
};

void log_hex(const char *what, const uint8_t *data, size_t len) {
    char text[3 * HAL_BLE_MAX_PAYLOAD + 1] = {0};
    for (size_t i = 0; i < len && i < HAL_BLE_MAX_PAYLOAD; i++) {
        std::snprintf(&text[3 * i], 4, "%02x ", data[i]);
    }
    ESP_LOGI(TAG, "%s: %s", what, text);
}

bool is_sensor(uint8_t type) { return type == WEDO_TYPE_TILT || type == WEDO_TYPE_MOTION; }
}  // namespace

void Hub::fill_config(hal_ble_central_cfg_t *cfg) {
    cfg->service = kService;
    cfg->chrs = kChrs;
    cfg->n_chrs = kChrCount;
    cfg->notify_mask = static_cast<uint8_t>((1u << kChrAttachedIo) | (1u << kChrInputValues));
}

bool Hub::take_dirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
}

void Hub::clear_ports() {
    for (unsigned i = 0; i < WEDO_EXT_PORTS; i++) {
        ports_[i] = {};
        format_pending_[i] = false;
    }
}

bool Hub::send(uint8_t chr, const uint8_t *data, size_t len) {
    const hal_result_t r = hal_ble_central_write(chr, data, len);
    if (r != HAL_OK && r != HAL_ERR_NO_MEM && r != HAL_ERR_BUSY) {
        ESP_LOGW(TAG, "write to chr %u failed: %s", chr, hal_result_name(r));
    }
    return r == HAL_OK;
}

void Hub::on_state(hal_ble_state_t state, uint32_t now_ms) {
    state_ = state;
    dirty_ = true;
    switch (state) {
        case HAL_BLE_IDLE:
            // A motor key held across a lost link must not restart the motor on reconnect.
            drive_ = 0;
            tone_pending_ = false;
            tone_stop_pending_ = false;
            clear_ports();
            break;
        case HAL_BLE_CONNECTING:
            clear_ports();
            break;
        case HAL_BLE_READY:
            led_mode_pending_ = true;
            led_pending_ = true;
            keepalive_ms_ = now_ms;
            ESP_LOGI(TAG, "hub ready");
            break;
        default:
            break;
    }
}

void Hub::on_notify(uint8_t chr, const uint8_t *data, size_t len, uint32_t now_ms) {
    if (chr == kChrAttachedIo) {
        log_hex("attached_io", data, len);
        wedo_attach_t a;
        if (!wedo_parse_attach(data, len, &a) || a.port < 1 || a.port > WEDO_EXT_PORTS) return;
        const unsigned i = a.port - 1u;
        ports_[i] = {};
        ports_[i].type = a.type;
        format_pending_[i] = a.attached && is_sensor(a.type);
        dirty_ = true;
        ESP_LOGI(TAG, "port %u: %s type %u", a.port, a.attached ? "attached" : "detached", a.type);
        return;
    }
    if (chr == kChrInputValues) {
        wedo_value_t v;
        if (!wedo_parse_value(data, len, &v) || v.port < 1 || v.port > WEDO_EXT_PORTS) {
            log_hex("input_values (ignored)", data, len);
            return;
        }
        const unsigned i = v.port - 1u;
        if (now_ms - value_log_ms_[i] >= kValueLogMs) {
            value_log_ms_[i] = now_ms;
            log_hex("input_values", data, len);
        }
        if (!is_sensor(ports_[i].type)) return;
        if (!ports_[i].has_value || std::memcmp(ports_[i].raw, v.raw, sizeof v.raw) != 0) dirty_ = true;
        ports_[i].has_value = true;
        std::memcpy(ports_[i].raw, v.raw, sizeof v.raw);
        return;
    }
    // kChrLowVoltage: value of the keepalive read; nothing to do with it.
}

void Hub::tick(uint32_t now_ms) {
    if (state_ == HAL_BLE_IDLE) {
        if (!scan_tried_ || now_ms - scan_try_ms_ >= kScanRetryMs) {
            scan_tried_ = true;
            scan_try_ms_ = now_ms;
            (void)hal_ble_central_scan();
        }
        return;
    }
    if (state_ != HAL_BLE_READY) return;

    uint8_t b[WEDO_CMD_MAX];

    // Motors first: a stop must never wait behind anything else.
    for (unsigned i = 0; i < WEDO_EXT_PORTS; i++) {
        if (ports_[i].type != WEDO_TYPE_MOTOR) continue;
        const int want = drive_ * power_;
        if (want == ports_[i].power) continue;
        const size_t n = wedo_cmd_motor(b, static_cast<uint8_t>(i + 1u), want);
        if (!send(kChrOutputCommand, b, n)) return;
        ESP_LOGI(TAG, "port %u motor %d", i + 1u, want);
        ports_[i].power = want;
        dirty_ = true;
    }
    if (tone_stop_pending_) {
        if (!send(kChrOutputCommand, b, wedo_cmd_tone_stop(b))) return;
        tone_stop_pending_ = false;
    }
    if (led_mode_pending_) {
        if (!send(kChrInputCommand, b, wedo_cmd_led_mode_rgb(b))) return;
        led_mode_pending_ = false;
    }
    for (unsigned i = 0; i < WEDO_EXT_PORTS; i++) {
        if (!format_pending_[i]) continue;
        const size_t n = wedo_cmd_sensor_format(b, static_cast<uint8_t>(i + 1u), ports_[i].type);
        if (n != 0 && !send(kChrInputCommand, b, n)) return;
        ESP_LOGI(TAG, "port %u: sensor notifications requested", i + 1u);
        format_pending_[i] = false;
    }
    if (led_pending_) {
        if (!send(kChrOutputCommand, b, wedo_cmd_led(b, led_[0], led_[1], led_[2]))) return;
        ESP_LOGI(TAG, "led %02x%02x%02x", led_[0], led_[1], led_[2]);
        led_pending_ = false;
    }
    if (tone_pending_) {
        // Sent without acknowledgement, as in the byte-level capture of a real hub
        // (cpseager/WeDo2-BLE-Protocol); falls back to an acknowledged write.
        // Known gap: the "LPF2 Smart Hub" clone (firmware 1.0.09.0000) accepts
        // this and stays silent, for every frequency and command id tried.
        const size_t n = wedo_cmd_tone(b, tone_hz_, tone_ms_);
        const hal_result_t r = hal_ble_central_write_no_ack(kChrOutputCommand, b, n);
        if (r == HAL_ERR_UNSUPPORTED) {
            if (!send(kChrOutputCommand, b, n)) return;
        } else if (r != HAL_OK) {
            return;
        }
        ESP_LOGI(TAG, "tone %u Hz %u ms (%s)", tone_hz_, tone_ms_, r == HAL_OK ? "no ack" : "ack");
        tone_pending_ = false;
    }
    if (now_ms - keepalive_ms_ >= kKeepaliveMs) {
        keepalive_ms_ = now_ms;
        (void)hal_ble_central_read(kChrLowVoltage);
    }
}

void Hub::set_drive(int dir) {
    const int d = dir > 0 ? 1 : (dir < 0 ? -1 : 0);
    if (d != drive_) dirty_ = true;
    drive_ = d;
}

void Hub::set_power(int power) {
    if (power < kPowerMin) power = kPowerMin;
    if (power > kPowerMax) power = kPowerMax;
    if (power != power_) dirty_ = true;
    power_ = power;
}

void Hub::set_led(uint8_t r, uint8_t g, uint8_t b) {
    led_[0] = r;
    led_[1] = g;
    led_[2] = b;
    led_pending_ = true;
}

void Hub::play_tone(uint16_t hz, uint16_t ms) {
    tone_hz_ = hz;
    tone_ms_ = ms;
    tone_pending_ = true;
    tone_stop_pending_ = false;
}

void Hub::stop_all() {
    set_drive(0);
    tone_pending_ = false;
    tone_stop_pending_ = true;
}

}  // namespace wedo
