#pragma once

// Конфиг по умолчанию для Home Assistant REST клиента.
// РЕКОМЕНДАЦИЯ: замените значения на свои, либо вызывайте ha_client_init()
// с вашими параметрами, игнорируя эти дефолты.

#ifndef HA_DEFAULT_BASE_URL
#define HA_DEFAULT_BASE_URL "http://192.168.1.185:8123/api/"
#endif

// ВАЖНО: создайте Long-Lived Access Token в Home Assistant (Профиль -> Tokens)
// и вставьте сюда. Хранить токен в прошивке — компромисс, лучше класть в NVS.
#ifndef HA_DEFAULT_TOKEN
#define HA_DEFAULT_TOKEN    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIwZDkwNzJjNDg4OGU0ZDg4YmQ5ODUwMjlhZWUxN2MzMyIsImlhdCI6MTc2Mjc3MTY4NiwiZXhwIjoyMDc4MTMxNjg2fQ.Xw1TCkP4hIZPlsR8Kbe-pug0kBIcvJoRuquxW-QVg1U"
#endif

// Если используете HTTPS с самоподписанным сертификатом, положите PEM сюда
// или передавайте через ha_client_init(). Иначе оставьте NULL.
#ifndef HA_DEFAULT_CA_CERT
#define HA_DEFAULT_CA_CERT  NULL
#endif

