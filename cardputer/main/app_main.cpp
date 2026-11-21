// SPDX-License-Identifier: AGPL-3.0-or-later
// app_main.cpp - supervisor only: boot, init, start the pinned tasks.
//
// Core plan (ESP32-S3, two cores):
//   core 0 (PRO): BLE controller + NimBLE host task (sdkconfig)
//   core 1 (APP): keyboard scan (10 ms period) and control / screen task

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#include "app_controller.hpp"

namespace {
constexpr char TAG[] = "main";

#if SOC_CPU_CORES_NUM >= 2
constexpr BaseType_t kCoreApp = 1;
#else
constexpr BaseType_t kCoreApp = 0;
#endif

constexpr uint32_t kKbdStack = 3072;
constexpr uint32_t kCtrlStack = 6144;
}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "WeDo 2.0 controller for Cardputer, cores=%d", SOC_CPU_CORES_NUM);
    if (!app::init()) ESP_LOGE(TAG, "init reported errors (see above)");

    if (xTaskCreatePinnedToCore(app::keyboard_task, "kbd", kKbdStack, nullptr, tskIDLE_PRIORITY + 5, nullptr,
                                kCoreApp) != pdPASS ||
        xTaskCreatePinnedToCore(app::control_task, "ctrl", kCtrlStack, nullptr, tskIDLE_PRIORITY + 4, nullptr,
                                kCoreApp) != pdPASS) {
        ESP_LOGE(TAG, "task creation failed");
    }
}
