#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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

// Возвращает отметку времени (us, esp_timer_get_time) последнего успешного HTTP
int64_t ha_client_get_last_ok_us(void);

// Есть ли сейчас выполняющийся HTTP‑запрос (наблюдательный флаг)
bool ha_client_is_busy(void);

// Время последней активности HTTP (us)
int64_t ha_client_get_last_activity_us(void);

// Не выполнять статус‑проверку до этого момента (us), используется как «кулдаун» после POST
int64_t ha_client_get_no_status_until_us(void);

#ifdef __cplusplus
}
#endif
