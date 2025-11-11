#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

// Простая обёртка над REST API Home Assistant (HTTP/HTTPS)

// base_url: например, "http://192.168.1.10:8123" или "https://ha.local:8123"
// token: Long-Lived Access Token (Profile -> Long-Lived Access Tokens)
// ca_cert_pem: PEM сертификат CA для HTTPS, либо NULL для HTTP/бандла
esp_err_t ha_client_init(const char *base_url, const char *token, const char *ca_cert_pem);

// GET /api/ (ожидается HTTP 200 и текст "API running.")
esp_err_t ha_get_status(int *http_status_opt);

// GET /api/states/{entity_id}
esp_err_t ha_get_state(const char *entity_id,
                       char *resp_buf, size_t resp_buf_len,
                       int *http_status_opt);

// POST /api/services/{domain}/{service} с JSON телом
esp_err_t ha_call_service(const char *domain, const char *service,
                          const char *json_body,
                          char *resp_buf, size_t resp_buf_len,
                          int *http_status_opt);

// Утилита: toggle для entity_id (определяет domain из префикса)
esp_err_t ha_toggle(const char *entity_id, int *http_status_opt);

// Онлайн по последнему успешному запросу (свежесть ~30с)
bool ha_client_is_online(void);

