#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Инициализация Wi‑Fi (STA), событий и сетевого стека
esp_err_t wifi_manager_init(void);

// Сканирует эфир и подключается к лучшей доступной сети из конфигурации
esp_err_t wifi_manager_connect_best_known(int32_t min_rssi);

// Ожидание получения IP (true если получили IP за отведённое время)
bool wifi_manager_wait_ip(int wait_ms);

// Старт фонового цикла: если не подключены, периодически сканирует и пытается
// подключиться к лучшей из известных сетей. Параметры:
//  - min_rssi: минимальный допустимый RSSI
//  - scan_interval_ms: пауза между попытками при отсутствии подключения
void wifi_manager_start_auto(int32_t min_rssi, int scan_interval_ms);

// Флаг наличия IP (подключены и получили IP)
bool wifi_manager_is_connected(void);
