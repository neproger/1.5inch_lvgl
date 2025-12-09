#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

namespace dht11
{

/**
 * @brief Read temperature and humidity from a DHT11 sensor.
 *
 * @param gpio           GPIO connected to DHT11 DATA pin.
 * @param out_humidity   Pointer to store relative humidity (%).
 * @param out_temperature Pointer to store temperature (°C).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t read(gpio_num_t gpio, int *out_humidity, int *out_temperature);

} // namespace dht11

