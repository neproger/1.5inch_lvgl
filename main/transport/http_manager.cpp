#include "http_manager.hpp"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_manager.h"
#include "http_utils.h"
#include "state_manager.hpp"
#include "config_server/config_store.hpp"
#include "app/app_config.hpp"
#include "app/app_state.hpp"

#include <cstring>
#include <string>

namespace http_manager
{

    namespace
    {

        static const char *TAG = "http_mgr";

        // When set to true, bootstrap_state() should stop retrying and return.
        static volatile bool s_cancel_bootstrap = false;

        struct HaHttpConfig
        {
            std::string url;
            std::string token;
            std::string host; // Raw host from config_store (without scheme transformation)
            std::uint16_t http_port{0};
        };

        // Last HA HTTP endpoint that responded successfully (bootstrap or weather).
        static std::string s_last_http_host;
        static std::uint16_t s_last_http_port = 0;

        // Build a list of HTTP connections from config_store (web UI),
        // falling back to compile-time defaults if none are stored.
        // Returns number of filled entries in out_cfg (up to max_cfg).
        static int build_http_configs(HaHttpConfig *out_cfg, int max_cfg)
        {
            if (!out_cfg || max_cfg <= 0)
            {
                return 0;
            }

            int out_count = 0;

            config_store::HaConn stored[4];
            std::size_t stored_count = 0;
            if (config_store::load_ha(stored, 4, stored_count) == ESP_OK && stored_count > 0)
            {
                for (std::size_t i = 0; i < stored_count && out_count < max_cfg; ++i)
                {
                    const auto &c = stored[i];
                    if (!c.host[0])
                    {
                        continue;
                    }

                    HaHttpConfig cfg;
                    std::string host = c.host;
                    cfg.host = c.host;
                    const std::uint16_t http_port = c.http_port ? c.http_port : 8123;
                    cfg.http_port = http_port;
                    if (host.rfind("http://", 0) == 0 || host.rfind("https://", 0) == 0)
                    {
                        cfg.url = host;
                    }
                    else
                    {
                        cfg.url = "http://";
                        cfg.url += host;
                    }
                    if (http_port != 0)
                    {
                        cfg.url += ":";
                        cfg.url += std::to_string(http_port);
                    }
                    // Always use /api/template for bootstrap and weather
                    if (cfg.url.find("/api/template") == std::string::npos)
                    {
                        if (!cfg.url.empty() && cfg.url.back() != '/')
                        {
                            cfg.url += "/api/template";
                        }
                        else
                        {
                            cfg.url += "api/template";
                        }
                    }
                    cfg.token = c.http_token;
                    out_cfg[out_count++] = cfg;
                }
            }

            return out_count;
        }

        static const char *kBootstrapTemplateBody = R"json(
{"template": "AREA_ID,AREA_NAME,ENTITY_ID,ENTITY_NAME,STATE\n{% for area in areas() -%}\n{% for e in area_entities(area) -%}\n{% if e.startswith('light.') or e.startswith('switch.') or e.startswith('input_boolean.') %}\n{{ area }},{{ area_name(area) }},{{ e }},{{ states[e].name }},{{ states[e].state }}\n{% endif %}\n{% endfor %}\n{% endfor %}"})json";

        // Weather template (used by screensaver).
        static const char *kWeatherTemplateBody = R"json(
{"template":"Temperature,Condition,Year,Month,Day,Weekday,Hour,Minute,Second\n{% set w = states['weather.forecast_home_assistant'] %}\n{{ w.attributes.temperature if w else 'N/A' }},{{ w.state if w else 'N/A' }},{{ now().year }},{{ now().month }},{{ now().day }},{{ now().weekday() }},{{ now().strftime('%H') }},{{ now().strftime('%M') }},{{ now().strftime('%S') }}"})json";

        static TaskHandle_t s_weather_task = nullptr;

        static bool ensure_wifi_connected()
        {
            if (wifi_manager_is_connected())
            {
                return true;
            }

            esp_err_t err = wifi_manager_connect_best_known(-85);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "WiFi connect failed: %s", esp_err_to_name(err));
                return false;
            }

            if (!wifi_manager_wait_ip(15000))
            {
                ESP_LOGW(TAG, "WiFi connect timeout");
                return false;
            }
            return true;
        }

        static bool perform_bootstrap_request()
        {
            if (s_cancel_bootstrap)
            {
                ESP_LOGW(TAG, "Bootstrap cancelled before HTTP attempts");
                return false;
            }

            HaHttpConfig cfgs[4];
            const int cfg_count = build_http_configs(cfgs, 4);
            char buf[2048];

            for (int i = 0; i < cfg_count; ++i)
            {
                if (s_cancel_bootstrap)
                {
                    ESP_LOGW(TAG, "Bootstrap cancelled during HTTP attempts");
                    return false;
                }

                int status = 0;
                const char *token = (!cfgs[i].token.empty() && cfgs[i].token[0]) ? cfgs[i].token.c_str() : nullptr;
                ESP_LOGI(TAG, "Bootstrap: trying HA server %d/%d at %s", i + 1, cfg_count, cfgs[i].url.c_str());

                esp_err_t err = http_send("POST",
                                          cfgs[i].url.c_str(),
                                          kBootstrapTemplateBody,
                                          "application/json",
                                          token,
                                          buf,
                                          sizeof(buf),
                                          &status);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "Bootstrap HTTP error (server %d): %s", i + 1, esp_err_to_name(err));
                    continue;
                }
                if (status < 200 || status >= 300)
                {
                    ESP_LOGW(TAG, "Bootstrap HTTP status %d (server %d)", status, i + 1);
                    continue;
                }
                if (!state::init_from_csv(buf, std::strlen(buf)))
                {
                    ESP_LOGW(TAG, "Failed to parse bootstrap CSV (server %d); proceeding with empty state", i + 1);
                }
                else
                {
                    ESP_LOGI(TAG, "State initialized from server %d: %d areas, %d entities",
                             i + 1,
                             (int)state::areas().size(),
                             (int)state::entities().size());
                }

                // Remember which HTTP endpoint worked last.
                s_last_http_host = cfgs[i].host;
                s_last_http_port = cfgs[i].http_port;
                ESP_LOGI(TAG, "Bootstrap HTTP ok (status %d, server %d)", status, i + 1);
                return true;
            }

            ESP_LOGW(TAG, "Bootstrap: all HA servers failed");
            return false;
        }

        static void trim_ws(std::string &s)
        {
            size_t start = 0;
            while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n'))
                ++start;
            size_t end = s.size();
            while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
                --end;
            s.assign(s.begin() + static_cast<long>(start), s.begin() + static_cast<long>(end));
        }

        static bool parse_weather_csv(const char *csv,
                                      float &out_temp,
                                      std::string &out_cond,
                                      int &out_year,
                                      int &out_month,
                                      int &out_day,
                                      int &out_weekday,
                                      int &out_hour,
                                      int &out_minute,
                                      int &out_second)
        {
            if (!csv)
                return false;

            std::string data(csv);
            std::string line;
            size_t pos = 0;

            auto next_line = [&](std::string &out) -> bool
            {
                if (pos >= data.size())
                    return false;
                size_t start = pos;
                while (pos < data.size() && data[pos] != '\n' && data[pos] != '\r')
                    ++pos;
                size_t end = pos;
                while (pos < data.size() && (data[pos] == '\n' || data[pos] == '\r'))
                    ++pos;
                out.assign(data.begin() + static_cast<long>(start), data.begin() + static_cast<long>(end));
                return true;
            };

            // Skip header line
            if (!next_line(line))
                return false;

            // Find first non-empty data line
            std::string data_line;
            while (next_line(data_line))
            {
                trim_ws(data_line);
                if (!data_line.empty())
                    break;
            }

            if (data_line.empty())
                return false;

            // Expect CSV: Temperature,Condition,Year,Month,Day,Weekday,Hour,Minute,Second
            std::string fields[9];
            size_t field_idx = 0;
            size_t start = 0;
            for (size_t i = 0; i <= data_line.size(); ++i)
            {
                bool at_end = (i == data_line.size());
                if (at_end || data_line[i] == ',')
                {
                    if (field_idx >= 9)
                        break;
                    std::string f = data_line.substr(start, i - start);
                    trim_ws(f);
                    fields[field_idx++] = std::move(f);
                    start = i + 1;
                }
            }

            if (field_idx < 2)
                return false;

            std::string temp_str = fields[0];
            std::string cond_str = fields[1];
            trim_ws(temp_str);
            trim_ws(cond_str);

            char *endp = nullptr;
            float temp = std::strtof(temp_str.c_str(), &endp);
            if (endp == temp_str.c_str())
            {
                // Failed to parse number
                return false;
            }

            out_temp = temp;
            out_cond = std::move(cond_str);

            auto parse_int_default = [](const std::string &s, int def) -> int
            {
                if (s.empty())
                    return def;
                char *endp_local = nullptr;
                long v = std::strtol(s.c_str(), &endp_local, 10);
                if (endp_local == s.c_str())
                    return def;
                return static_cast<int>(v);
            };

            out_year = (field_idx > 2) ? parse_int_default(fields[2], 0) : 0;
            out_month = (field_idx > 3) ? parse_int_default(fields[3], 0) : 0;
            out_day = (field_idx > 4) ? parse_int_default(fields[4], 0) : 0;
            out_weekday = (field_idx > 5) ? parse_int_default(fields[5], 0) : 0;
            out_hour = (field_idx > 6) ? parse_int_default(fields[6], 0) : 0;
            out_minute = (field_idx > 7) ? parse_int_default(fields[7], 0) : 0;
            out_second = (field_idx > 8) ? parse_int_default(fields[8], 0) : 0;

            return true;
        }

        static void weather_task(void *arg)
        {
            (void)arg;
            char buf[256];

            const TickType_t kErrorDelayTicks = pdMS_TO_TICKS(2000);
            const TickType_t kPollIntervalTicks = pdMS_TO_TICKS(app_config::kWeatherPollIntervalMs);

            for (;;)
            {
                if (!wifi_manager_is_connected() || g_app_state == AppState::NormalScreensaver)
                {
                    vTaskDelay(kErrorDelayTicks);
                    continue;
                }

                HaHttpConfig cfgs[4];
                const int cfg_count = build_http_configs(cfgs, 4);
                bool ok = false;

                for (int i = 0; i < cfg_count; ++i)
                {
                    int status = 0;
                    const char *token = (!cfgs[i].token.empty() && cfgs[i].token[0]) ? cfgs[i].token.c_str() : nullptr;
                    ESP_LOGI(TAG, "Weather: trying HA server %d/%d at %s", i + 1, cfg_count, cfgs[i].url.c_str());

                    esp_err_t err = http_send("POST",
                                              cfgs[i].url.c_str(),
                                              kWeatherTemplateBody,
                                              "application/json",
                                              token,
                                              buf,
                                              sizeof(buf),
                                              &status);
                    if (err != ESP_OK)
                    {
                        ESP_LOGW(TAG, "Weather HTTP error (server %d): %s", i + 1, esp_err_to_name(err));
                        continue;
                    }
                    if (status < 200 || status >= 300)
                    {
                        ESP_LOGW(TAG, "Weather HTTP status %d (server %d)", status, i + 1);
                        continue;
                    }

                    // Remember which HTTP endpoint worked last.
                    s_last_http_host = cfgs[i].host;
                    s_last_http_port = cfgs[i].http_port;

                    ok = true;
                    break;
                }

                if (!ok)
                {
                    ESP_LOGW(TAG, "Weather: all HA servers failed");
                    vTaskDelay(kErrorDelayTicks);
                    continue;
                }

                float temp_c = 0.0f;
                std::string cond;
                int year = 0;
                int month = 0;
                int day = 0;
                int weekday = 0;
                int hour = 0;
                int minute = 0;
                int second = 0;
                if (!parse_weather_csv(buf,
                                       temp_c,
                                       cond,
                                       year,
                                       month,
                                       day,
                                       weekday,
                                       hour,
                                       minute,
                                       second))
                {
                    ESP_LOGW(TAG, "Failed to parse weather CSV");
                    vTaskDelay(kErrorDelayTicks);
                    continue;
                }

                state::set_weather(temp_c, cond);
                state::set_clock(year, month, day, weekday, hour, minute, second, esp_timer_get_time());
                vTaskDelay(kPollIntervalTicks);
            }

            s_weather_task = nullptr;
            vTaskDelete(nullptr);
        }

    } // namespace

    bool bootstrap_state()
    {
        for (int attempt = 1;; ++attempt)
        {
            if (s_cancel_bootstrap)
            {
                ESP_LOGW(TAG, "Bootstrap cancelled");
                return false;
            }
            if (!ensure_wifi_connected())
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            if (perform_bootstrap_request())
            {
                return true;
            }
            ESP_LOGW(TAG, "Bootstrap attempt %d failed", attempt);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        return false;
    }

    void cancel_bootstrap()
    {
        s_cancel_bootstrap = true;
    }

    void start_weather_polling()
    {
        if (s_weather_task == nullptr)
        {
            // Weather task does HTTP requests, string parsing, etc.,
            // so give it a slightly larger stack to avoid overflow.
            xTaskCreate(weather_task, "weather", 6144, nullptr, 3, &s_weather_task);
        }
    }

    bool get_last_successful_http_host(std::string &host, std::uint16_t &http_port)
    {
        if (s_last_http_host.empty() || s_last_http_port == 0)
        {
            return false;
        }
        host = s_last_http_host;
        http_port = s_last_http_port;
        return true;
    }

} // namespace http_manager
