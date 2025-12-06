#pragma once

#include <stddef.h>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>

namespace state
{

    struct Area
    {
        std::string id;
        std::string name;
    };

    struct Entity
    {
        std::string id;
        std::string name;
        std::string state;
        std::string area_id;
    };

    struct WeatherState
    {
        float temperature_c = 0.0f;
        std::string condition;
        bool valid = false;
    };

    struct DhtState
    {
        int temperature_c = 0;
        int humidity = 0;
        bool valid = false;
    };

    struct ClockState
    {
        int year = 0;
        int month = 0;
        int day = 0;
        int weekday = 0;             // 0=Monday..6=Sunday (follow Python weekday())
        std::int64_t base_seconds = 0;      // seconds since start of day at sync time
        std::int64_t sync_monotonic_us = 0; // esp_timer_get_time() at sync time
        bool valid = false;
    };

    using EntityListener = std::function<void(const Entity &)>;

    // Parse initial state from CSV (bootstrap HTTP response).
    // Returns true on success, false on parse error.
    bool init_from_csv(const char *csv, size_t len);

    // Update entity state by ID; notifies listeners if value changed.
    bool set_entity_state(const std::string &entity_id, const std::string &state);

    // Accessors
    const std::vector<Area> &areas();
    const std::vector<Entity> &entities();
    const Entity *find_entity(const std::string &id);

    // Weather state
    void set_weather(float temperature_c, const std::string &condition);
    WeatherState weather();

    // Local DHT11 sensor state
    void set_dht(int temperature_c, int humidity);
    DhtState dht();

    // Clock state (time/date synced from HA)
    void set_clock(int year,
                   int month,
                   int day,
                   int weekday,
                   int hour,
                   int minute,
                   int second,
                   std::int64_t monotonic_us);
    ClockState clock();

    // UI subscriptions
    int subscribe_entity(const std::string &id, EntityListener cb);
    void unsubscribe(int subscription_id);

} // namespace state
