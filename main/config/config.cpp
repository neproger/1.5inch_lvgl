#include "config/config.hpp"

#include <cstring>
#include <string>
#include <cstdio>
#include "esp_log.h"

namespace config
{

    namespace
    {

        extern "C"
        {
            extern const uint8_t _binary_home_assistant_yaml_start[];
            extern const uint8_t _binary_home_assistant_yaml_end[];
        }

        constexpr const char *TAG = "config";

        HaSettings s_cfg{};
        bool s_loaded = false;

        inline void copy_field(char *dst, size_t len, const char *src)
        {
            if (!dst || len == 0)
                return;
            if (!src)
                src = "";
            std::strncpy(dst, src, len - 1);
            dst[len - 1] = '\0';
        }

        inline std::string trim(const std::string &s)
        {
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                return "";
            size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }

        inline std::string unquote(const std::string &s)
        {
            if (s.size() >= 2)
            {
                char first = s.front();
                char last = s.back();
                if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
                {
                    return s.substr(1, s.size() - 2);
                }
            }
            return s;
        }

        void reset_settings()
        {
            std::memset(&s_cfg, 0, sizeof(s_cfg));
        }

        bool push_entity(const Entity &e)
        {
            if (e.entity_id[0] == '\0')
            {
                ESP_LOGW(TAG, "Пропущена сущность без id");
                return false;
            }
            if (s_cfg.entity_count >= kMaxEntities)
            {
                ESP_LOGW(TAG, "Достигнут лимит сущностей (%d)", kMaxEntities);
                return false;
            }
            s_cfg.entities[s_cfg.entity_count++] = e;
            return true;
        }

        bool parse_yaml(const char *data, size_t len)
        {
            enum class Section
            {
                None,
                MQTT,
                Entities
            };
            Section section = Section::None;
            Entity current{};
            bool entity_active = false;

            size_t pos = 0;
            while (pos < len)
            {
                size_t line_end = pos;
                while (line_end < len && data[line_end] != '\n' && data[line_end] != '\r')
                {
                    ++line_end;
                }
                std::string line(data + pos, line_end - pos);
                pos = line_end;
                while (pos < len && (data[pos] == '\n' || data[pos] == '\r'))
                {
                    ++pos;
                }

                size_t comment = line.find('#');
                if (comment != std::string::npos)
                {
                    line = line.substr(0, comment);
                }
                line = trim(line);
                if (line.empty())
                {
                    continue;
                }

                bool list_item = false;
                if (line.rfind("- ", 0) == 0)
                {
                    list_item = true;
                    line = trim(line.substr(2));
                }
                else if (line == "-")
                {
                    list_item = true;
                    line.clear();
                }

                if (line == "mqtt:")
                {
                    section = Section::MQTT;
                    continue;
                }
                if (line == "entities:")
                {
                    if (entity_active)
                    {
                        push_entity(current);
                        entity_active = false;
                    }
                    section = Section::Entities;
                    continue;
                }

                size_t colon = line.find(':');
                if (colon == std::string::npos)
                {
                    continue;
                }

                std::string key = trim(line.substr(0, colon));
                std::string value = trim(line.substr(colon + 1));
                value = unquote(value);

                if (section == Section::MQTT)
                {
                    if (key == "uri")
                    {
                        copy_field(s_cfg.broker_uri, sizeof(s_cfg.broker_uri), value.c_str());
                    }
                    else if (key == "username")
                    {
                        copy_field(s_cfg.username, sizeof(s_cfg.username), value.c_str());
                    }
                    else if (key == "password")
                    {
                        copy_field(s_cfg.password, sizeof(s_cfg.password), value.c_str());
                    }
                    else if (key == "client_id")
                    {
                        copy_field(s_cfg.client_id, sizeof(s_cfg.client_id), value.c_str());
                    }
                    else if (key == "base_topic")
                    {
                        copy_field(s_cfg.base_topic, sizeof(s_cfg.base_topic), value.c_str());
                    }
                    continue;
                }

                if (section == Section::Entities)
                {
                    if (list_item)
                    {
                        if (entity_active)
                        {
                            push_entity(current);
                        }
                        std::memset(&current, 0, sizeof(current));
                        entity_active = true;
                    }
                    else if (!entity_active)
                    {
                        // игнорируем поля вне элемента списка
                        continue;
                    }

                    if (key == "name")
                    {
                        copy_field(current.name, sizeof(current.name), value.c_str());
                    }
                    else if (key == "id")
                    {
                        copy_field(current.entity_id, sizeof(current.entity_id), value.c_str());
                    }
                    else if (key == "type")
                    {
                        copy_field(current.type, sizeof(current.type), value.c_str());
                    }
                }
            }

            if (entity_active)
            {
                push_entity(current);
            }

            return true;
        }

    } // namespace

    esp_err_t load()
    {
        if (s_loaded)
        {
            return ESP_OK;
        }

        const char *text = reinterpret_cast<const char *>(_binary_home_assistant_yaml_start);
        size_t len = _binary_home_assistant_yaml_end - _binary_home_assistant_yaml_start;
        if (!text || len == 0)
        {
            ESP_LOGE(TAG, "Не найден встроенный YAML");
            return ESP_ERR_INVALID_STATE;
        }

        reset_settings();
        if (!parse_yaml(text, len))
        {
            return ESP_ERR_INVALID_STATE;
        }

        if (s_cfg.broker_uri[0] == '\0')
        {
            ESP_LOGE(TAG, "Не указан mqtt.uri");
            return ESP_ERR_INVALID_STATE;
        }
        if (s_cfg.base_topic[0] == '\0')
        {
            copy_field(s_cfg.base_topic, sizeof(s_cfg.base_topic), "ha");
        }

        std::snprintf(s_cfg.status_topic, sizeof(s_cfg.status_topic), "%s/ui/status", s_cfg.base_topic);
        s_cfg.status_topic[sizeof(s_cfg.status_topic) - 1] = '\0';
        std::snprintf(s_cfg.toggle_topic, sizeof(s_cfg.toggle_topic), "%s/cmd/toggle", s_cfg.base_topic);
        s_cfg.toggle_topic[sizeof(s_cfg.toggle_topic) - 1] = '\0';
        std::snprintf(s_cfg.state_prefix, sizeof(s_cfg.state_prefix), "%s/state/", s_cfg.base_topic);
        s_cfg.state_prefix[sizeof(s_cfg.state_prefix) - 1] = '\0';

        if (s_cfg.entity_count == 0)
        {
            ESP_LOGE(TAG, "Список entities пуст");
            return ESP_ERR_INVALID_STATE;
        }

        s_loaded = true;
        return ESP_OK;
    }

    const HaSettings &ha()
    {
        if (!s_loaded)
        {
            (void)load();
        }
        return s_cfg;
    }

} // namespace config
