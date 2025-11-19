#include "http_manager.hpp"

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_manager.h"
#include "http_utils.h"
#include "ha_http_config.h"
#include "state_manager.hpp"

#include <cstring>
#include <string>


namespace http_manager
{

    namespace
    {

        static const char *TAG = "http_mgr";

        // Bootstrap template (areas/entities).
        static constexpr const char *kBootstrapUrl = HA_HTTP_BOOTSTRAP_URL;
        static constexpr const char *kBootstrapToken = HA_HTTP_BEARER_TOKEN;

        static const char *kBootstrapTemplateBody = R"json(
{
  "template": "AREA_ID,AREA_NAME,ENTITY_ID,ENTITY_NAME,STATE\n{% for area in areas() -%}\n{% for e in area_entities(area) -%}\n{{ area }},{{ area_name(area) }},{{ e }},{{ states[e].name }},{{ states[e].state }}\n{% endfor %}\n{% endfor %}"
}
)json";

        // Weather template (used by screensaver).
        static constexpr const char *kWeatherUrl = HA_HTTP_BOOTSTRAP_URL;
        static constexpr const char *kWeatherToken = HA_HTTP_BEARER_TOKEN;
        static const char *kWeatherTemplateBody = R"json(
{
  "template": "Temperature,Condition\n{% set w = states['weather.forecast_home_assistant'] %}\n{{ w.attributes.temperature if w else 'N/A' }}°C, {{ w.state if w else 'N/A' }}"
}
)json";

        static TaskHandle_t s_weather_task = NULL;
        static void (*s_weather_cb)(void) = nullptr;

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
            char buf[2048];
            int status = 0;
            const char *token = (kBootstrapToken && kBootstrapToken[0]) ? kBootstrapToken : nullptr;
            esp_err_t err = http_send("POST", kBootstrapUrl, kBootstrapTemplateBody, "application/json", token, buf, sizeof(buf), &status);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "Bootstrap HTTP error: %s", esp_err_to_name(err));
                return false;
            }
            if (status < 200 || status >= 300)
            {
                ESP_LOGW(TAG, "Bootstrap HTTP status %d", status);
                return false;
            }
            if (!state::init_from_csv(buf, std::strlen(buf)))
            {
                ESP_LOGW(TAG, "Failed to parse bootstrap CSV; proceeding with empty state");
            }
            else
            {
                ESP_LOGI(TAG, "State initialized: %d areas, %d entities",
                         (int)state::areas().size(),
                         (int)state::entities().size());
            }
            ESP_LOGI(TAG, "Bootstrap HTTP ok (status %d)", status);
            return true;
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

        static bool parse_weather_csv(const char *csv, float &out_temp, std::string &out_cond)
        {
            if (!csv)
                return false;

            std::string data(csv);
            std::string line;
            size_t pos = 0;

            auto next_line = [&](std::string &out) -> bool {
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

            size_t comma = data_line.find(',');
            if (comma == std::string::npos)
                return false;

            std::string temp_str = data_line.substr(0, comma);
            std::string cond_str = data_line.substr(comma + 1);
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
            return true;
        }

        static void weather_task(void *arg)
        {
            (void)arg;
            char buf[256];

            for (;;)
            {
                if (!wifi_manager_is_connected())
                {
                    continue;
                }

                int status = 0;
                const char *token = (kWeatherToken && kWeatherToken[0]) ? kWeatherToken : nullptr;
                esp_err_t err = http_send("POST", kWeatherUrl, kWeatherTemplateBody, "application/json", token, buf, sizeof(buf), &status);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "Weather HTTP error: %s", esp_err_to_name(err));
                    continue;
                }
                if (status < 200 || status >= 300)
                {
                    ESP_LOGW(TAG, "Weather HTTP status %d", status);
                    continue;
                }

                float temp_c = 0.0f;
                std::string cond;
                if (!parse_weather_csv(buf, temp_c, cond))
                {
                    ESP_LOGW(TAG, "Failed to parse weather CSV");
                    continue;
                }

                state::set_weather(temp_c, cond);

                if (s_weather_cb)
                {
                    s_weather_cb();
                }
                vTaskDelay(pdMS_TO_TICKS(50000));
            }
        }

    } // namespace

    bool bootstrap_state()
    {
        static constexpr int kMaxAttempts = 3;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
        {
            if (!ensure_wifi_connected())
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            if (perform_bootstrap_request())
            {
                return true;
            }
            ESP_LOGW(TAG, "Bootstrap attempt %d/%d failed", attempt, kMaxAttempts);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        return false;
    }

    void start_weather_polling(void (*on_weather_updated)(void))
    {
        s_weather_cb = on_weather_updated;
        if (s_weather_task == NULL)
        {
            xTaskCreate(weather_task, "weather", 4096, NULL, 3, &s_weather_task);
        }
    }

} // namespace http_manager

