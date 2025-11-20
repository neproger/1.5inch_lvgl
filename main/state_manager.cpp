#include "state_manager.hpp"

#include "esp_log.h"

#include <unordered_map>
#include <array>
#include <cctype>
#include <cstring>
#include <mutex>

namespace state
{

    namespace
    {

        constexpr const char *TAG = "state";

        std::vector<Area> g_areas;
        std::vector<Entity> g_entities;
        std::unordered_map<std::string, size_t> g_area_index_by_id;
        std::unordered_map<std::string, size_t> g_entity_index_by_id;
        WeatherState g_weather;
        ClockState g_clock;

        struct ListenerEntry
        {
            int id;
            std::string entity_id;
            EntityListener cb;
        };

        std::vector<ListenerEntry> g_listeners;
        int g_next_listener_id = 1;
        std::mutex g_mutex;

        static void trim(std::string &s)
        {
            size_t start = 0;
            while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
                ++start;
            size_t end = s.size();
            while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
                --end;
            if (start == 0 && end == s.size())
                return;
            s.assign(s.begin() + static_cast<long>(start), s.begin() + static_cast<long>(end));
        }

        static std::vector<std::string> split_lines(const char *data, size_t len)
        {
            std::vector<std::string> lines;
            if (!data || len == 0)
                return lines;

            size_t start = 0;
            for (size_t i = 0; i < len; ++i)
            {
                char c = data[i];
                if (c == '\n')
                {
                    size_t line_len = (i > start) ? (i - start) : 0;
                    if (line_len > 0)
                    {
                        std::string line(data + start, line_len);
                        lines.push_back(std::move(line));
                    }
                    start = i + 1;
                }
            }
            // last line (no trailing '\n')
            if (start < len)
            {
                std::string line(data + start, len - start);
                lines.push_back(std::move(line));
            }
            return lines;
        }

        static bool split_csv_line(const std::string &line, std::array<std::string, 5> &fields_out)
        {
            fields_out = {};
            size_t field_idx = 0;
            size_t start = 0;
            for (size_t i = 0; i <= line.size(); ++i)
            {
                bool at_end = (i == line.size());
                if (at_end || line[i] == ',')
                {
                    if (field_idx >= fields_out.size())
                    {
                        // too many fields
                        return false;
                    }
                    std::string f = line.substr(start, i - start);
                    trim(f);
                    fields_out[field_idx++] = std::move(f);
                    start = i + 1;
                }
            }
            return field_idx == fields_out.size();
        }

    } // namespace

    bool init_from_csv(const char *csv, size_t len)
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        g_areas.clear();
        g_entities.clear();
        g_area_index_by_id.clear();
        g_entity_index_by_id.clear();

        if (!csv || len == 0)
        {
            ESP_LOGW(TAG, "init_from_csv: empty input");
            return false;
        }

        auto lines = split_lines(csv, len);
        if (lines.empty())
        {
            ESP_LOGW(TAG, "init_from_csv: no lines");
            return false;
        }

        // Find header line
        size_t header_idx = 0;
        for (; header_idx < lines.size(); ++header_idx)
        {
            std::string hdr = lines[header_idx];
            trim(hdr);
            if (!hdr.empty())
                break;
        }
        if (header_idx >= lines.size())
        {
            ESP_LOGW(TAG, "init_from_csv: header not found");
            return false;
        }

        std::array<std::string, 5> fields;
        if (!split_csv_line(lines[header_idx], fields))
        {
            ESP_LOGW(TAG, "init_from_csv: failed to parse header");
            return false;
        }

        auto eq_nocase = [](const std::string &a, const char *b)
        {
            size_t blen = std::strlen(b);
            if (a.size() != blen)
                return false;
            for (size_t i = 0; i < blen; ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        };

        if (!eq_nocase(fields[0], "AREA_ID") ||
            !eq_nocase(fields[1], "AREA_NAME") ||
            !eq_nocase(fields[2], "ENTITY_ID") ||
            !eq_nocase(fields[3], "ENTITY_NAME") ||
            !eq_nocase(fields[4], "STATE"))
        {
            ESP_LOGW(TAG, "init_from_csv: unexpected header '%s'", lines[header_idx].c_str());
            return false;
        }

        // Parse data lines
        for (size_t line_idx = header_idx + 1; line_idx < lines.size(); ++line_idx)
        {
            std::string line = lines[line_idx];
            trim(line);
            if (line.empty())
                continue;

            std::array<std::string, 5> cols;
            if (!split_csv_line(line, cols))
            {
                ESP_LOGW(TAG, "init_from_csv: skip malformed line %d: '%s'", static_cast<int>(line_idx), line.c_str());
                continue;
            }

            const std::string &area_id = cols[0];
            const std::string &area_name = cols[1];
            const std::string &entity_id = cols[2];
            const std::string &entity_name = cols[3];
            const std::string &state = cols[4];

            if (entity_id.empty())
                continue;

            // Area
            size_t area_index = 0;
            auto it_area = g_area_index_by_id.find(area_id);
            if (it_area == g_area_index_by_id.end())
            {
                Area a;
                a.id = area_id;
                a.name = area_name;
                area_index = g_areas.size();
                g_areas.push_back(std::move(a));
                g_area_index_by_id[area_id] = area_index;
            }
            else
            {
                area_index = it_area->second;
                (void)area_index;
            }

            // Entity
            if (g_entity_index_by_id.find(entity_id) != g_entity_index_by_id.end())
            {
                // duplicate id, skip
                continue;
            }

            Entity e;
            e.id = entity_id;
            e.name = entity_name;
            e.state = state;
            e.area_id = area_id;

            size_t ent_index = g_entities.size();
            g_entities.push_back(std::move(e));
            g_entity_index_by_id[entity_id] = ent_index;
        }

        ESP_LOGI(TAG, "parsed %d areas, %d entities",
                 static_cast<int>(g_areas.size()),
                 static_cast<int>(g_entities.size()));
        return true;
    }

    bool set_entity_state(const std::string &entity_id, const std::string &state)
    {
        std::vector<EntityListener> listeners_to_call;
        size_t index = 0;

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            auto it = g_entity_index_by_id.find(entity_id);
            if (it == g_entity_index_by_id.end())
                return false;

            index = it->second;
            Entity &e = g_entities[index];
            if (e.state == state)
                return true;

            e.state = state;

            // Collect listeners to call outside the lock
            for (const auto &entry : g_listeners)
            {
                if (entry.cb && entry.entity_id == entity_id)
                {
                    listeners_to_call.push_back(entry.cb);
                }
            }
        }

        const Entity &e = g_entities[index];
        for (const auto &cb : listeners_to_call)
        {
            cb(e);
        }

        return true;
    }

    const std::vector<Area> &areas()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_areas;
    }

    const std::vector<Entity> &entities()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_entities;
    }

    const Entity *find_entity(const std::string &id)
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        auto it = g_entity_index_by_id.find(id);
        if (it == g_entity_index_by_id.end())
            return nullptr;
        return &g_entities[it->second];
    }

    void set_weather(float temperature_c, const std::string &condition)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_weather.temperature_c = temperature_c;
        g_weather.condition = condition;
        g_weather.valid = true;
    }

    WeatherState weather()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_weather;
    }

    void set_clock(int year,
                   int month,
                   int day,
                   int weekday,
                   int hour,
                   int minute,
                   int second,
                   std::int64_t monotonic_us)
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        // Basic range clamping
        if (hour < 0)
            hour = 0;
        if (hour > 23)
            hour = 23;
        if (minute < 0)
            minute = 0;
        if (minute > 59)
            minute = 59;
        if (second < 0)
            second = 0;
        if (second > 59)
            second = 59;

        g_clock.year = year;
        g_clock.month = month;
        g_clock.day = day;
        g_clock.weekday = weekday;
        g_clock.base_seconds = static_cast<std::int64_t>(hour) * 3600 +
                               static_cast<std::int64_t>(minute) * 60 +
                               static_cast<std::int64_t>(second);
        g_clock.sync_monotonic_us = monotonic_us;
        g_clock.valid = true;
    }

    ClockState clock()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_clock;
    }

    int subscribe_entity(const std::string &id, EntityListener cb)
    {
        if (!cb)
            return 0;
        std::lock_guard<std::mutex> lock(g_mutex);
        ListenerEntry e;
        e.id = g_next_listener_id++;
        e.entity_id = id;
        e.cb = std::move(cb);
        g_listeners.push_back(std::move(e));
        return g_next_listener_id - 1;
    }

    void unsubscribe(int subscription_id)
    {
        if (subscription_id <= 0)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto it = g_listeners.begin(); it != g_listeners.end(); ++it)
        {
            if (it->id == subscription_id)
            {
                g_listeners.erase(it);
                return;
            }
        }
    }

} // namespace state
