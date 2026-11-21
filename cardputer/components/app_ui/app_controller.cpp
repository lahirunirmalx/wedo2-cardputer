// SPDX-License-Identifier: AGPL-3.0-or-later
// app_controller.cpp - see app_controller.hpp.

#include "app_controller.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "board_cardputer.h"
#include "display.hpp"
#include "hal_audio.h"
#include "hal_ble_central.h"
#include "keyboard.hpp"
#include "screen.hpp"
#include "theme.hpp"
#include "wedo_hub.hpp"

namespace app {
namespace {
constexpr char TAG[] = "app";

constexpr TickType_t kScanPeriod = pdMS_TO_TICKS(10);
constexpr TickType_t kControlWait = pdMS_TO_TICKS(20);
constexpr uint32_t kStatsMs = 30000;
constexpr unsigned kQueueDepth = 48;

constexpr uint8_t kKeyUp = ';';
constexpr uint8_t kKeyDown = '.';
constexpr uint8_t kKeyLeft = ',';
constexpr uint8_t kKeyRight = '/';

constexpr uint8_t kKeyLockFwd = 'f';
constexpr uint8_t kKeyLockRev = 'b';

// Notes sound on the Cardputer's own speaker for as long as the key is held
// (this hub ignores tone commands). The hub tone command is still sent, so a
// hub that does implement it beeps as well.
constexpr uint16_t kNoteMaxMs = 5000;     // a stuck key cannot sound forever
constexpr uint16_t kHubToneMs = 300;
constexpr uint8_t kKeyVolDown = '-';
constexpr uint8_t kKeyVolUp = '=';
constexpr int kVolumeMin = 10;
constexpr int kVolumeMax = 100;
constexpr int kVolumeStep = 10;
constexpr int kVolumeDefault = 60;
constexpr uint16_t kBlipHz = 880;         // feedback when the volume changes
constexpr uint16_t kBlipMs = 80;
constexpr uint32_t kSpeakerRate = 16000;
struct Note {
    uint8_t key;
    const char *name;
    uint16_t hz;
};
constexpr Note kNotes[] = {
    {'q', "C5", 523}, {'w', "D5", 587}, {'e', "E5", 659}, {'r', "F5", 698},
    {'t', "G5", 784}, {'y', "A5", 880}, {'u', "B5", 988}, {'i', "C6", 1047},
};

constexpr unsigned kDefaultLed = 6;  // blue

enum MsgKind : uint8_t { kMsgKey, kMsgBleState, kMsgBleNotify };
struct Msg {
    uint8_t kind;
    uint8_t a;     // key code | state | characteristic index
    uint8_t b;     // key: 1 = down
    uint8_t len;
    uint8_t data[HAL_BLE_MAX_PAYLOAD];
};

QueueHandle_t g_queue = nullptr;
std::atomic<uint32_t> g_dropped{0};
TaskHandle_t g_kbd_task = nullptr;

wedo::Hub g_hub;
bool g_up_held = false;
bool g_down_held = false;
int g_lock = 0;   // continuous run: +1 forward, -1 reverse, 0 off
unsigned g_led = kDefaultLed;
const char *g_note_name = nullptr;   // shown on the TONE card (static string)
uint8_t g_note_key = 0;              // note key being held, 0 = none
int g_volume = kVolumeDefault;
bool g_tone_on = false;          // a tone was started and may still be sounding
uint32_t g_tone_until_ms = 0;
bool g_ui_dirty = true;

uint32_t now_ms() { return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS; }

void post(const Msg &m) {
    if (g_queue == nullptr || xQueueSend(g_queue, &m, 0) != pdTRUE) g_dropped.fetch_add(1);
}

// --- producers (BLE host task, keyboard task): only post -------------------

void on_ble_state(hal_ble_state_t state, void *) {
    Msg m = {};
    m.kind = kMsgBleState;
    m.a = static_cast<uint8_t>(state);
    post(m);
}

void on_ble_notify(uint8_t chr, const uint8_t *data, size_t len, void *) {
    Msg m = {};
    m.kind = kMsgBleNotify;
    m.a = chr;
    m.len = static_cast<uint8_t>(len > sizeof m.data ? sizeof m.data : len);
    std::memcpy(m.data, data, m.len);
    post(m);
}

void on_key_event(const kbd::Event &event, void *) {
    Msg m = {};
    m.kind = kMsgKey;
    m.a = event.key;
    m.b = event.down ? 1 : 0;
    post(m);
}

// --- screen ----------------------------------------------------------------

screen::Link to_link(hal_ble_state_t st) {
    switch (st) {
        case HAL_BLE_SCANNING:    return screen::Link::Scanning;
        case HAL_BLE_CONNECTING:  return screen::Link::Connecting;
        case HAL_BLE_DISCOVERING: return screen::Link::Setup;
        case HAL_BLE_READY:       return screen::Link::Ready;
        default:                  return screen::Link::Idle;
    }
}

screen::Port to_port(const wedo::PortView &v) {
    screen::Port p;
    p.type_id = v.type;
    p.has_value = v.has_value;
    switch (v.type) {
        case WEDO_TYPE_NONE:
            p.kind = screen::PortKind::Empty;
            break;
        case WEDO_TYPE_MOTOR:
            p.kind = screen::PortKind::Motor;
            p.power = v.power;
            break;
        case WEDO_TYPE_TILT:
            p.kind = screen::PortKind::Tilt;
            p.tilt_x = static_cast<int8_t>(v.raw[0]);
            p.tilt_y = static_cast<int8_t>(v.raw[1]);
            break;
        case WEDO_TYPE_MOTION:
            p.kind = screen::PortKind::Motion;
            p.level = v.raw[0];
            break;
        default:
            p.kind = screen::PortKind::Unknown;
            break;
    }
    return p;
}

screen::Model build_model(uint32_t now) {
    screen::Model m;
    m.link = to_link(g_hub.state());
    m.port[0] = to_port(g_hub.port(0));
    m.port[1] = to_port(g_hub.port(1));
    m.power = g_hub.power();
    m.drive = g_hub.drive();
    m.locked = g_lock != 0 && g_hub.drive() == g_lock;
    m.led = g_led;
    m.note = g_note_name;
    m.tone_playing = g_tone_on && static_cast<int32_t>(g_tone_until_ms - now) > 0;
    return m;
}

// --- key handling ------------------------------------------------------------

// A held arrow wins; with no arrow held the continuous-run direction applies.
void apply_drive() {
    const int held = (g_up_held ? 1 : 0) - (g_down_held ? 1 : 0);
    g_hub.set_drive(held != 0 ? held : g_lock);
    g_ui_dirty = true;
}

void on_key(uint8_t key, bool down) {
    if (key >= 0x20 && key < 0x7F) {
        ESP_LOGI(TAG, "key '%c' %s", key, down ? "down" : "up");
    } else {
        ESP_LOGI(TAG, "key 0x%02x %s", key, down ? "down" : "up");
    }
    switch (key) {
        case kKeyUp:
            g_up_held = down;
            apply_drive();
            return;
        case kKeyDown:
            g_down_held = down;
            apply_drive();
            return;
        default:
            break;
    }
    if (!down) {
        if (g_note_key != 0 && key == g_note_key) {
            g_note_key = 0;
            g_tone_on = false;
            (void)hal_audio_stop();
            g_ui_dirty = true;
        }
        return;
    }
    if (key == kKeyLeft) {
        g_hub.set_power(g_hub.power() - wedo::kPowerStep);
    } else if (key == kKeyRight) {
        g_hub.set_power(g_hub.power() + wedo::kPowerStep);
    } else if (key == kKeyLockFwd || key == kKeyLockRev) {
        const int dir = key == kKeyLockFwd ? 1 : -1;
        g_lock = (g_lock == dir) ? 0 : dir;   // same key again stops
        apply_drive();
    } else if (key == kKeyVolDown || key == kKeyVolUp) {
        g_volume += key == kKeyVolUp ? kVolumeStep : -kVolumeStep;
        if (g_volume < kVolumeMin) g_volume = kVolumeMin;
        if (g_volume > kVolumeMax) g_volume = kVolumeMax;
        ESP_LOGI(TAG, "volume %d", g_volume);
        (void)hal_audio_tone(kBlipHz, kBlipMs, static_cast<uint8_t>(g_volume));
    } else if (key == ' ') {
        g_up_held = false;
        g_down_held = false;
        g_lock = 0;
        g_tone_on = false;
        g_note_key = 0;
        (void)hal_audio_stop();
        g_hub.stop_all();
        g_ui_dirty = true;
    } else if (key >= '0' && key <= '9') {
        g_led = static_cast<unsigned>(key - '0');
        const theme::LedPreset &led = theme::led_preset(g_led);
        g_hub.set_led(led.r, led.g, led.b);
        g_ui_dirty = true;
    } else {
        for (const Note &n : kNotes) {
            if (n.key != key) continue;
            g_note_name = n.name;
            g_note_key = key;
            g_tone_on = true;
            g_tone_until_ms = now_ms() + kNoteMaxMs;
            (void)hal_audio_tone(n.hz, kNoteMaxMs, static_cast<uint8_t>(g_volume));
            g_hub.play_tone(n.hz, kHubToneMs);
            g_ui_dirty = true;
            break;
        }
    }
}

void handle(const Msg &m) {
    switch (m.kind) {
        case kMsgKey:
            on_key(m.a, m.b != 0);
            break;
        case kMsgBleState:
            g_hub.on_state(static_cast<hal_ble_state_t>(m.a), now_ms());
            if (m.a == HAL_BLE_IDLE) {
                g_up_held = false;
                g_down_held = false;
                g_lock = 0;   // never restart a motor by itself after a reconnect
            }
            break;
        case kMsgBleNotify:
            g_hub.on_notify(m.a, m.data, m.len, now_ms());
            break;
        default:
            break;
    }
}
}  // namespace

bool init() {
    g_queue = xQueueCreate(kQueueDepth, sizeof(Msg));
    if (g_queue == nullptr) {
        ESP_LOGE(TAG, "no memory for the event queue");
        return false;
    }
    bool ok = true;
    if (!display::init()) {
        ESP_LOGE(TAG, "display init failed (continuing without a screen)");
        ok = false;
    }
    if (!kbd::init()) {
        ESP_LOGE(TAG, "keyboard init failed");
        ok = false;
    }
    const hal_audio_cfg_t spk = {BOARD_SPK_BCLK, BOARD_SPK_WS, BOARD_SPK_DOUT, kSpeakerRate};
    const hal_result_t ar = hal_audio_init(&spk);
    if (ar != HAL_OK) {
        ESP_LOGE(TAG, "speaker init failed: %s (continuing without sound)", hal_result_name(ar));
        ok = false;
    } else {
        (void)hal_audio_tone(kBlipHz, kBlipMs, static_cast<uint8_t>(g_volume));   // short power-on blip
    }
    const theme::LedPreset &led = theme::led_preset(g_led);
    g_hub.set_led(led.r, led.g, led.b);

    hal_ble_central_cfg_t cfg = {};
    wedo::Hub::fill_config(&cfg);
    cfg.on_state = on_ble_state;
    cfg.on_notify = on_ble_notify;
    const hal_result_t r = hal_ble_central_start(&cfg);
    if (r != HAL_OK) {
        ESP_LOGE(TAG, "BLE start failed: %s", hal_result_name(r));
        ok = false;
    }
    return ok;
}

void keyboard_task(void *) {
    g_kbd_task = xTaskGetCurrentTaskHandle();
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        kbd::scan(on_key_event, nullptr);
        vTaskDelayUntil(&wake, kScanPeriod);
    }
}

void control_task(void *) {
    uint32_t stats_ms = now_ms();
    uint32_t frame_ms = stats_ms;
    screen::reset();
    for (;;) {
        Msg m;
        if (xQueueReceive(g_queue, &m, kControlWait) == pdTRUE) {
            handle(m);
            while (xQueueReceive(g_queue, &m, 0) == pdTRUE) handle(m);
        }
        const uint32_t now = now_ms();
        g_hub.tick(now);

        // render() only redraws the cards whose content changed, so calling it
        // on every animation frame costs nothing while the screen is still.
        const bool frame_due = now - frame_ms >= screen::kFrameMs;
        if (g_hub.take_dirty() || g_ui_dirty || frame_due) {
            g_ui_dirty = false;
            const screen::Model model = build_model(now);
            if (frame_due) {
                frame_ms = now;
                screen::step(model);
            }
            screen::render(model);
        }
        if (now - stats_ms >= kStatsMs) {
            stats_ms = now;
            ESP_LOGI(TAG, "stack free: ctrl %u, kbd %u words; events dropped %u",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                     g_kbd_task != nullptr ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_kbd_task)) : 0u,
                     static_cast<unsigned>(g_dropped.load()));
        }
    }
}

}  // namespace app
