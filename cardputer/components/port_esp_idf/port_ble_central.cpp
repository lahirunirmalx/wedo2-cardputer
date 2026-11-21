// SPDX-License-Identifier: AGPL-3.0-or-later
// port_ble_central.cpp - ESP-IDF (NimBLE host) implementation of
// hal_ble_central.h.
//
// One connection. Flow: active scan -> advert or scan response carries the
// wanted 128-bit service -> connect -> discover every characteristic ->
// for each one in notify_mask find its CCCD and write 0x0001 -> READY.
// The CCCD handle is discovered, never assumed to be val_handle + 1.
//
// All GATT callbacks run on the NimBLE host task. The write / read / scan
// entry points may be called from any task (the host API locks itself).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

#include "hal_ble_central.h"
#include "port_err.h"

namespace {
constexpr char TAG[] = "port_ble";
constexpr int kMaxAllChrs = 48;
constexpr int32_t kConnectTimeoutMs = 10000;

struct Target {
    ble_uuid128_t uuid;
    uint16_t val_handle;
    uint16_t cccd_handle;
    uint8_t props;
};

hal_ble_central_cfg_t g_cfg = {};
ble_uuid128_t g_service = {};
Target g_t[HAL_BLE_MAX_CHRS] = {};
uint16_t g_def_handles[kMaxAllChrs] = {};
int g_n_def = 0;
uint8_t g_own_addr_type = 0;
bool g_started = false;
std::atomic<bool> g_synced{false};
std::atomic<uint16_t> g_conn{BLE_HS_CONN_HANDLE_NONE};
std::atomic<hal_ble_state_t> g_state{HAL_BLE_IDLE};

// Identity dump after each connect (diagnostics): the standard GAP, Device
// Information and Battery characteristics found during discovery are read
// once and logged.
struct InfoChr {
    uint16_t uuid16;
    uint16_t val_handle;
};
constexpr int kMaxInfo = 10;
InfoChr g_info[kMaxInfo] = {};
int g_n_info = 0;

int gap_event(struct ble_gap_event *event, void *arg);
void subscribe_next(uint16_t conn, unsigned from);
void read_info_next(uint16_t conn, int index);

const char *info_name(uint16_t uuid16) {
    switch (uuid16) {
        case 0x2A00: return "device name";
        case 0x2A19: return "battery %";
        case 0x2A24: return "model";
        case 0x2A25: return "serial";
        case 0x2A26: return "firmware rev";
        case 0x2A27: return "hardware rev";
        case 0x2A28: return "software rev";
        case 0x2A29: return "manufacturer";
        default:     return nullptr;
    }
}

void set_state(hal_ble_state_t s) {
    g_state.store(s);
    if (g_cfg.on_state != nullptr) g_cfg.on_state(s, g_cfg.user);
}

ble_uuid128_t to_nimble(const hal_ble_uuid128_t &u) {
    ble_uuid128_t out = {};
    out.u.type = BLE_UUID_TYPE_128;
    for (int i = 0; i < 16; i++) out.value[i] = u.b[15 - i];  // NimBLE stores little-endian
    return out;
}

hal_result_t map_rc(int rc) {
    switch (rc) {
        case 0:               return HAL_OK;
        case BLE_HS_ENOMEM:   return HAL_ERR_NO_MEM;
        case BLE_HS_EBUSY:    return HAL_ERR_BUSY;
        case BLE_HS_ENOTCONN: return HAL_ERR_BUSY;
        case BLE_HS_EINVAL:   return HAL_ERR_INVALID_ARG;
        default:              return HAL_ERR_INTERNAL;
    }
}

void fail(uint16_t conn, const char *why, int rc) {
    ESP_LOGE(TAG, "setup failed: %s (rc=%d), dropping the link", why, rc);
    if (ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM) != 0) {
        g_conn.store(BLE_HS_CONN_HANDLE_NONE);
        set_state(HAL_BLE_IDLE);
    }
}

int on_cccd_written(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *, void *arg) {
    const unsigned i = static_cast<unsigned>(reinterpret_cast<uintptr_t>(arg));
    if (error->status != 0) {
        fail(conn, "CCCD write", error->status);
        return 0;
    }
    ESP_LOGI(TAG, "chr %u: notifications on", i);
    subscribe_next(conn, i + 1);
    return 0;
}

int on_dsc(uint16_t conn, const struct ble_gatt_error *error, uint16_t, const struct ble_gatt_dsc *dsc, void *arg) {
    const unsigned i = static_cast<unsigned>(reinterpret_cast<uintptr_t>(arg));
    if (error->status == 0 && dsc != nullptr) {
        if (g_t[i].cccd_handle == 0 && ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            g_t[i].cccd_handle = dsc->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail(conn, "descriptor discovery", error->status);
        return 0;
    }
    if (g_t[i].cccd_handle == 0) {
        fail(conn, "no CCCD on a notify characteristic", 0);
        return 0;
    }
    static const uint8_t k_enable[2] = {0x01, 0x00};
    const int rc = ble_gattc_write_flat(conn, g_t[i].cccd_handle, k_enable, sizeof k_enable, on_cccd_written, arg);
    if (rc != 0) fail(conn, "CCCD write start", rc);
    return 0;
}

void subscribe_next(uint16_t conn, unsigned from) {
    for (unsigned i = from; i < g_cfg.n_chrs; i++) {
        if ((g_cfg.notify_mask & (1u << i)) == 0) continue;
        uint16_t end = 0xFFFF;
        for (int k = 0; k < g_n_def; k++) {
            if (g_def_handles[k] > g_t[i].val_handle && g_def_handles[k] - 1 < end) {
                end = static_cast<uint16_t>(g_def_handles[k] - 1);
            }
        }
        if (end <= g_t[i].val_handle) {
            fail(conn, "notify characteristic has no descriptors", 0);
            return;
        }
        const int rc = ble_gattc_disc_all_dscs(conn, g_t[i].val_handle, end, on_dsc,
                                               reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        if (rc != 0) fail(conn, "descriptor discovery start", rc);
        return;
    }
    ESP_LOGI(TAG, "ready");
    set_state(HAL_BLE_READY);
    read_info_next(conn, 0);
}

int on_info_read(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg) {
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    if (error->status == 0 && attr != nullptr && attr->om != nullptr) {
        uint8_t buf[32];
        uint16_t len = OS_MBUF_PKTLEN(attr->om);
        if (len > sizeof buf) len = sizeof buf;
        if (os_mbuf_copydata(attr->om, 0, len, buf) == 0) {
            char text[sizeof buf + 1];
            char hex[3 * sizeof buf + 1] = {0};
            for (uint16_t k = 0; k < len; k++) {
                text[k] = (buf[k] >= 0x20 && buf[k] < 0x7F) ? static_cast<char>(buf[k]) : '.';
                snprintf(&hex[3 * k], 4, "%02x ", buf[k]);
            }
            text[len] = '\0';
            ESP_LOGI(TAG, "info %s (0x%04x): \"%s\"  [%s]", info_name(g_info[i].uuid16), g_info[i].uuid16, text, hex);
        }
    } else {
        ESP_LOGW(TAG, "info 0x%04x: read failed, status %d", g_info[i].uuid16, error->status);
    }
    // A plain read reports once (no BLE_HS_EDONE follow-up): go on to the next.
    read_info_next(conn, i + 1);
    return 0;
}

void read_info_next(uint16_t conn, int index) {
    for (int i = index; i < g_n_info; i++) {
        const int rc = ble_gattc_read(conn, g_info[i].val_handle, on_info_read,
                                      reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        if (rc == 0) return;
        ESP_LOGW(TAG, "info 0x%04x: read not started, rc=%d", g_info[i].uuid16, rc);
    }
}

int on_chr(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *) {
    if (error->status == 0 && chr != nullptr) {
        if (g_n_def < kMaxAllChrs) g_def_handles[g_n_def++] = chr->def_handle;
        char text[BLE_UUID_STR_LEN];
        ESP_LOGI(TAG, "gatt: handle %3u props 0x%02x  %s", chr->val_handle, chr->properties,
                 ble_uuid_to_str(&chr->uuid.u, text));
        const uint16_t u16 = ble_uuid_u16(&chr->uuid.u);
        if (info_name(u16) != nullptr && g_n_info < kMaxInfo && (chr->properties & BLE_GATT_CHR_PROP_READ) != 0) {
            g_info[g_n_info++] = {u16, chr->val_handle};
        }
        for (unsigned i = 0; i < g_cfg.n_chrs; i++) {
            if (ble_uuid_cmp(&chr->uuid.u, &g_t[i].uuid.u) == 0) {
                g_t[i].val_handle = chr->val_handle;
                g_t[i].props = chr->properties;
                ESP_LOGI(TAG, "chr %u: handle %u props 0x%02x", i, chr->val_handle, chr->properties);
            }
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail(conn, "characteristic discovery", error->status);
        return 0;
    }
    for (unsigned i = 0; i < g_cfg.n_chrs; i++) {
        if (g_t[i].val_handle == 0) {
            ESP_LOGE(TAG, "chr %u not found on the peripheral", i);
            fail(conn, "missing characteristic", 0);
            return 0;
        }
    }
    subscribe_next(conn, 0);
    return 0;
}

int on_write_done(uint16_t, const struct ble_gatt_error *error, struct ble_gatt_attr *, void *) {
    if (error->status != 0) ESP_LOGW(TAG, "write failed: status %d", error->status);
    return 0;
}

int on_read_done(uint16_t, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg) {
    if (error->status != 0 || attr == nullptr || attr->om == nullptr) return 0;
    const unsigned i = static_cast<unsigned>(reinterpret_cast<uintptr_t>(arg));
    uint8_t buf[HAL_BLE_MAX_PAYLOAD];
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len > sizeof buf) len = sizeof buf;
    if (os_mbuf_copydata(attr->om, 0, len, buf) == 0 && g_cfg.on_notify != nullptr) {
        g_cfg.on_notify(static_cast<uint8_t>(i), buf, len, g_cfg.user);
    }
    return 0;
}

void on_disc(const struct ble_gap_event *event) {
    if (g_state.load() != HAL_BLE_SCANNING) return;
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, event->disc.data, event->disc.length_data) != 0) return;
    bool match = false;
    for (int i = 0; i < f.num_uuids128; i++) {
        if (ble_uuid_cmp(&f.uuids128[i].u, &g_service.u) == 0) match = true;
    }
    if (!match) return;

    const uint8_t *a = event->disc.addr.val;
    ESP_LOGI(TAG, "found %02x:%02x:%02x:%02x:%02x:%02x rssi %d, connecting", a[5], a[4], a[3], a[2], a[1], a[0],
             event->disc.rssi);
    (void)ble_gap_disc_cancel();
    const int rc = ble_gap_connect(g_own_addr_type, &event->disc.addr, kConnectTimeoutMs, nullptr, gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed: rc=%d", rc);
        set_state(HAL_BLE_IDLE);
        return;
    }
    set_state(HAL_BLE_CONNECTING);
}

void on_notify_rx(const struct ble_gap_event *event) {
    if (g_cfg.on_notify == nullptr || event->notify_rx.om == nullptr) return;
    for (unsigned i = 0; i < g_cfg.n_chrs; i++) {
        if (g_t[i].val_handle != event->notify_rx.attr_handle) continue;
        uint8_t buf[HAL_BLE_MAX_PAYLOAD];
        uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (len > sizeof buf) len = sizeof buf;
        if (os_mbuf_copydata(event->notify_rx.om, 0, len, buf) == 0) {
            g_cfg.on_notify(static_cast<uint8_t>(i), buf, len, g_cfg.user);
        }
        return;
    }
}

int gap_event(struct ble_gap_event *event, void *) {
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            on_disc(event);
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            if (g_state.load() == HAL_BLE_SCANNING) set_state(HAL_BLE_IDLE);
            return 0;

        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status != 0) {
                ESP_LOGW(TAG, "connect failed: status %d", event->connect.status);
                g_conn.store(BLE_HS_CONN_HANDLE_NONE);
                set_state(HAL_BLE_IDLE);
                return 0;
            }
            const uint16_t conn = event->connect.conn_handle;
            g_conn.store(conn);
            g_n_def = 0;
            g_n_info = 0;
            for (unsigned i = 0; i < HAL_BLE_MAX_CHRS; i++) {
                g_t[i].val_handle = 0;
                g_t[i].cccd_handle = 0;
                g_t[i].props = 0;
            }
            ESP_LOGI(TAG, "connected, discovering");
            set_state(HAL_BLE_DISCOVERING);
            const int rc = ble_gattc_disc_all_chrs(conn, 1, 0xFFFF, on_chr, nullptr);
            if (rc != 0) fail(conn, "characteristic discovery start", rc);
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "disconnected: reason %d", event->disconnect.reason);
            g_conn.store(BLE_HS_CONN_HANDLE_NONE);
            set_state(HAL_BLE_IDLE);
            return 0;

        case BLE_GAP_EVENT_NOTIFY_RX:
            on_notify_rx(event);
            return 0;

        default:
            return 0;
    }
}

void on_sync() {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "no usable BLE address: rc=%d", rc);
        return;
    }
    g_synced.store(true);
    ESP_LOGI(TAG, "host synced");
}

void on_reset(int reason) {
    ESP_LOGE(TAG, "host reset: reason %d", reason);
    g_synced.store(false);
}

void host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
}  // namespace

extern "C" hal_result_t hal_ble_central_start(const hal_ble_central_cfg_t *cfg) {
    if (g_started) return HAL_ERR_BUSY;
    if (cfg == nullptr || cfg->chrs == nullptr || cfg->n_chrs == 0 || cfg->n_chrs > HAL_BLE_MAX_CHRS) {
        return HAL_ERR_INVALID_ARG;
    }
    g_cfg = *cfg;
    g_service = to_nimble(cfg->service);
    char text[BLE_UUID_STR_LEN];
    ESP_LOGI(TAG, "service %s", ble_uuid_to_str(&g_service.u, text));
    for (unsigned i = 0; i < cfg->n_chrs; i++) {
        g_t[i] = {};
        g_t[i].uuid = to_nimble(cfg->chrs[i]);
        ESP_LOGI(TAG, "chr %u  %s%s", i, ble_uuid_to_str(&g_t[i].uuid.u, text),
                 (cfg->notify_mask & (1u << i)) != 0 ? "  (notify)" : "");
    }

    // The NVS partition may hold another firmware's settings: never erase it.
    // Without NVS the radio only loses its stored calibration.
    esp_err_t e = nvs_flash_init();
    if (e != ESP_OK) ESP_LOGW(TAG, "nvs_flash_init: %s (continuing, NVS left untouched)", esp_err_to_name(e));

    e = nimble_port_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(e));
        return port::map_err(e);
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
    g_started = true;
    return HAL_OK;
}

extern "C" hal_result_t hal_ble_central_scan(void) {
    if (!g_started || !g_synced.load() || g_state.load() != HAL_BLE_IDLE) return HAL_ERR_BUSY;
    struct ble_gap_disc_params p = {};
    p.passive = 0;            // active: the service UUID may sit in the scan response
    p.filter_duplicates = 0;
    const int rc = ble_gap_disc(g_own_addr_type, BLE_HS_FOREVER, &p, gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: rc=%d", rc);
        return map_rc(rc);
    }
    ESP_LOGI(TAG, "scanning");
    set_state(HAL_BLE_SCANNING);
    return HAL_OK;
}

extern "C" hal_result_t hal_ble_central_write(uint8_t chr, const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0 || len > HAL_BLE_MAX_PAYLOAD || chr >= g_cfg.n_chrs) return HAL_ERR_INVALID_ARG;
    if (g_state.load() != HAL_BLE_READY) return HAL_ERR_BUSY;
    const uint16_t conn = g_conn.load();
    const Target &t = g_t[chr];
    const uint16_t n = static_cast<uint16_t>(len);
    if ((t.props & BLE_GATT_CHR_PROP_WRITE) != 0) {
        return map_rc(ble_gattc_write_flat(conn, t.val_handle, data, n, on_write_done, nullptr));
    }
    return map_rc(ble_gattc_write_no_rsp_flat(conn, t.val_handle, data, n));
}

extern "C" hal_result_t hal_ble_central_write_no_ack(uint8_t chr, const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0 || len > HAL_BLE_MAX_PAYLOAD || chr >= g_cfg.n_chrs) return HAL_ERR_INVALID_ARG;
    if (g_state.load() != HAL_BLE_READY) return HAL_ERR_BUSY;
    const Target &t = g_t[chr];
    if ((t.props & BLE_GATT_CHR_PROP_WRITE_NO_RSP) == 0) return HAL_ERR_UNSUPPORTED;
    return map_rc(ble_gattc_write_no_rsp_flat(g_conn.load(), t.val_handle, data, static_cast<uint16_t>(len)));
}

extern "C" hal_result_t hal_ble_central_read(uint8_t chr) {
    if (chr >= g_cfg.n_chrs) return HAL_ERR_INVALID_ARG;
    if (g_state.load() != HAL_BLE_READY) return HAL_ERR_BUSY;
    return map_rc(ble_gattc_read(g_conn.load(), g_t[chr].val_handle, on_read_done,
                                 reinterpret_cast<void *>(static_cast<uintptr_t>(chr))));
}

extern "C" hal_result_t hal_ble_central_disconnect(void) {
    const uint16_t conn = g_conn.load();
    if (conn == BLE_HS_CONN_HANDLE_NONE) return HAL_OK;
    return map_rc(ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM));
}
