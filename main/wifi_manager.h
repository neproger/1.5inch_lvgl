#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Инициализация Wi‑Fi (STA), событий и сетевого стека
esp_err_t wifi_manager_init(void);

// Сканирует эфир и подключается к лучшей доступной сети из конфигурации
esp_err_t wifi_manager_connect_best_known(int32_t min_rssi);

// Ожидание получения IP (true если получили IP за отведённое время)
bool wifi_manager_wait_ip(int wait_ms);

