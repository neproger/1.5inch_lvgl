#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

// Простая обертка над REST API Home Assistant.
// Использует Long-Lived Access Token и HTTP(S) запросы.

// Инициализация клиента.
// base_url: например, "http://192.168.1.10:8123" или "https://ha.local:8123"
// token: Long-Lived Access Token ("Profile" -> "Long-Lived Access Tokens")
// ca_cert_pem: PEM сертификат CA (для HTTPS с самоподписанным сертификатом), иначе NULL
esp_err_t ha_client_init(const char *base_url, const char *token, const char *ca_cert_pem);

// Быстрая проверка доступности API (GET /api/), HTTP 200 ожидается
esp_err_t ha_get_status(int *http_status_opt);

// Универсальный вызов сервиса: POST /api/services/{domain}/{service} с произвольным JSON телом
esp_err_t ha_call_service(const char *domain, const char *service,
                          const char *json_body,
                          char *resp_buf, size_t resp_buf_len,
                          int *http_status_opt);

// Удобный хелпер: toggle по entity_id (например, "light.kitchen")
esp_err_t ha_toggle(const char *entity_id, int *http_status_opt);

// Получить состояние сущности: GET /api/states/{entity_id}, тело ответа JSON
esp_err_t ha_get_state(const char *entity_id,
                       char *resp_buf, size_t resp_buf_len,
                       int *http_status_opt);

// Periodic monitor of HA availability (GET /api/ every interval_ms)
// Start once; subsequent calls are no-ops. interval_ms <= 0 -> 5000 ms.
esp_err_t ha_client_start_monitor(int interval_ms);

// Latest monitor state (true when last probe returned HTTP 200)
bool ha_client_is_online(void);

// Start a dedicated HA worker task that serializes all HTTP requests through
// a single queue to improve stability under flaky WiFi/routers.
// Safe to call multiple times; queue_len recommends 4–8.
esp_err_t ha_client_start_worker(int queue_len);

