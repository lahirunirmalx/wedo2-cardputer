// SPDX-License-Identifier: AGPL-3.0-or-later
// hal_ble_central.h - vendor-neutral Bluetooth LE GATT central for ONE
// peripheral.
//
// Model:
//   - hal_ble_central_start() once: brings the stack up. State is IDLE.
//   - hal_ble_central_scan() from IDLE: active scan for a peripheral that
//     advertises cfg.service (in the advert or the scan response). The first
//     match is connected, the characteristics in cfg.chrs are resolved and
//     the ones in cfg.notify_mask are subscribed. Then the state is READY.
//   - on any failure or disconnect the state returns to IDLE; the caller
//     decides when to scan again.
//
// Callbacks run on the BLE host task: they must not block. Post to a queue.
// Notifications can arrive before READY (while later subscriptions are
// still being set up).
//
// Device implementation: components/port_esp_idf/port_ble_central.cpp.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_BLE_MAX_CHRS    8u
#define HAL_BLE_MAX_PAYLOAD 20u   /**< bytes per write / notification (ATT MTU 23) */

/** 128-bit UUID in the order it is written: b[0] is the first text byte. */
typedef struct {
    uint8_t b[16];
} hal_ble_uuid128_t;

typedef enum {
    HAL_BLE_IDLE = 0,
    HAL_BLE_SCANNING,
    HAL_BLE_CONNECTING,
    HAL_BLE_DISCOVERING,
    HAL_BLE_READY
} hal_ble_state_t;

typedef void (*hal_ble_state_cb)(hal_ble_state_t state, void *user);

/** @p chr is the index into cfg.chrs. Also called with the value of a read. */
typedef void (*hal_ble_notify_cb)(uint8_t chr, const uint8_t *data, size_t len, void *user);

typedef struct {
    hal_ble_uuid128_t        service;      /**< advertised service to look for */
    const hal_ble_uuid128_t *chrs;         /**< characteristics to resolve; must stay valid */
    uint8_t                  n_chrs;       /**< 1..HAL_BLE_MAX_CHRS */
    uint8_t                  notify_mask;  /**< bit i: subscribe to chrs[i] */
    hal_ble_state_cb         on_state;
    hal_ble_notify_cb        on_notify;
    void                    *user;
} hal_ble_central_cfg_t;

/** Start the BLE stack. HAL_ERR_BUSY if already started. */
hal_result_t hal_ble_central_start(const hal_ble_central_cfg_t *cfg);

/** Begin scanning. HAL_ERR_BUSY while the stack is not ready or not IDLE. */
hal_result_t hal_ble_central_scan(void);

/** Write to chrs[chr]. HAL_ERR_BUSY unless READY; HAL_ERR_NO_MEM when too many writes are in flight (retry). */
hal_result_t hal_ble_central_write(uint8_t chr, const uint8_t *data, size_t len);

/**
 * Write to chrs[chr] without asking the peripheral for an acknowledgement.
 * HAL_ERR_UNSUPPORTED when the characteristic does not allow it.
 */
hal_result_t hal_ble_central_write_no_ack(uint8_t chr, const uint8_t *data, size_t len);

/** Read chrs[chr]; the value arrives through on_notify. HAL_ERR_BUSY unless READY. */
hal_result_t hal_ble_central_read(uint8_t chr);

/** Drop the connection (no-op when not connected). */
hal_result_t hal_ble_central_disconnect(void);

#ifdef __cplusplus
}
#endif
