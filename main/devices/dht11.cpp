#include "devices/dht11.hpp"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

namespace
{

constexpr uint32_t DHT_START_SIGNAL_MS = 20;    // >18 ms
constexpr uint32_t DHT_RESPONSE_TIMEOUT_US = 200; // Safety margin
constexpr uint32_t DHT_BIT_TIMEOUT_US = 100;
constexpr uint32_t DHT_HIGH_THRESHOLD_US = 40; // >40us -> '1', <40us -> '0'

esp_err_t wait_for_level(gpio_num_t gpio, int level, uint32_t timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) != level)
    {
        if ((esp_timer_get_time() - start) > timeout_us)
        {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

} // namespace

esp_err_t dht11::read(gpio_num_t gpio, int *out_humidity, int *out_temperature)
{
    if (!out_humidity || !out_temperature)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Prepare line: pull-up input
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);

    // Start signal: pull low for at least 18ms
    gpio_set_level(gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(DHT_START_SIGNAL_MS));
    gpio_set_level(gpio, 1);

    // Wait 20–40us and switch to input
    esp_rom_delay_us(30);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);

    // DHT response: 80us low, 80us high
    esp_err_t err;
    err = wait_for_level(gpio, 0, DHT_RESPONSE_TIMEOUT_US);
    if (err != ESP_OK)
    {
        return err;
    }
    err = wait_for_level(gpio, 1, DHT_RESPONSE_TIMEOUT_US);
    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t data[5] = {0};

    for (int i = 0; i < 40; ++i)
    {
        // Each bit: 50us low
        err = wait_for_level(gpio, 0, DHT_BIT_TIMEOUT_US);
        if (err != ESP_OK)
        {
            return err;
        }
        // Start of high pulse
        err = wait_for_level(gpio, 1, DHT_BIT_TIMEOUT_US);
        if (err != ESP_OK)
        {
            return err;
        }

        int64_t start = esp_timer_get_time();
        while (gpio_get_level(gpio) == 1)
        {
            if ((esp_timer_get_time() - start) > DHT_BIT_TIMEOUT_US)
            {
                break;
            }
        }
        uint32_t high_us = static_cast<uint32_t>(esp_timer_get_time() - start);

        uint8_t bit = (high_us > DHT_HIGH_THRESHOLD_US) ? 1 : 0;
        int byte_index = i / 8;
        data[byte_index] <<= 1;
        data[byte_index] |= bit;
    }

    // Verify checksum
    uint8_t checksum = static_cast<uint8_t>(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4])
    {
        return ESP_ERR_INVALID_CRC;
    }

    // DHT11 format: data[0] = RH int, data[2] = temp int
    *out_humidity = static_cast<int>(data[0]);
    *out_temperature = static_cast<int>(data[2]);

    return ESP_OK;
}
