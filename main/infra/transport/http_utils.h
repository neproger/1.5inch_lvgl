#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Minimal HTTP helper for generic requests (GET/POST/etc).
// - method: "GET", "POST", "PUT", ...
// - url: full URL (http:// or https://)
// - body: optional request body (may be NULL)
// - content_type_opt: e.g., "application/json"; may be NULL
// - auth_bearer_token_opt: if non-NULL, sends Authorization: Bearer <token>
// - out/out_len: optional response buffer (may be NULL)
// - out_status_code_opt: optional HTTP status
esp_err_t http_send(const char *method,
                    const char *url,
                    const char *body,
                    const char *content_type_opt,
                    const char *auth_bearer_token_opt,
                    char *out,
                    size_t out_len,
                    int *out_status_code_opt);

#ifdef __cplusplus
}
#endif

