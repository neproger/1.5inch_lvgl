#include "devices_init.h"
#include "ui_app.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "dht.h"
#include <stdbool.h>

void app_main(void)
{
    if (devices_init() != ESP_OK) {
        return;
    }

    /* Create your UI under LVGL mutex */
    lvgl_port_lock(0);
    ui_app_init();
    lvgl_port_unlock();

    // Start SSR first (powers DHT), then DHT reader
    extern void start_dht_on_gpio39(void);
    extern void start_ssr40_blink(void);
    start_ssr40_blink();
    vTaskDelay(pdMS_TO_TICKS(200));
    start_dht_on_gpio39();
}

// ===== Application GPIO defines =====
#define APP_DHT_GPIO   GPIO_NUM_39
#define APP_SSR_GPIO   GPIO_NUM_40

// (GPIO38 logger removed)

// ===== DHT one-wire reader on GPIO39 =====
static esp_err_t dht_read_raw(uint8_t out[5])
{
    // 1) Pull line LOW for at least 18 ms
    gpio_set_direction(APP_DHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(APP_DHT_GPIO, 0);
    esp_rom_delay_us(20000);

    // 2) Release line and wait for sensor response
    gpio_set_direction(APP_DHT_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(APP_DHT_GPIO, GPIO_PULLUP_ONLY);

    // wait for sensor response edges
    int timeout;
    // wait for LOW (~80us)
    timeout = 1000; // us
    while (timeout-- > 0) { if (gpio_get_level(APP_DHT_GPIO) == 0) break; esp_rom_delay_us(1); }
    if (timeout <= 0) return ESP_ERR_TIMEOUT;
    // then HIGH (~80us)
    timeout = 200;
    while (timeout-- > 0) { if (gpio_get_level(APP_DHT_GPIO) == 1) break; esp_rom_delay_us(1); }
    if (timeout <= 0) return ESP_ERR_TIMEOUT;
    // then LOW to start bits
    timeout = 200;
    while (timeout-- > 0) { if (gpio_get_level(APP_DHT_GPIO) == 0) break; esp_rom_delay_us(1); }
    if (timeout <= 0) return ESP_ERR_TIMEOUT;

    // 3) Read 40 bits
    uint8_t data[5] = {0};
    for (int i = 0; i < 40; ++i) {
        // Each bit starts with ~50us LOW
        // wait rising edge to HIGH (start of bit)
        timeout = 100;
        while (timeout-- > 0) { if (gpio_get_level(APP_DHT_GPIO) == 1) break; esp_rom_delay_us(1); }
        if (timeout <= 0) return ESP_ERR_TIMEOUT;
        // Measure HIGH width to distinguish 0/1
        int t = 0;
        while (gpio_get_level(APP_DHT_GPIO)) {
            esp_rom_delay_us(1);
            if (++t > 120) break; // safety cap
        }
        // shift and set
        data[i/8] <<= 1;
        if (t > 45) data[i/8] |= 0x01; // >~45us => '1', else '0'
        // After falling edge, next bit follows
    }

    // 4) Checksum
    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) return ESP_ERR_INVALID_CRC;

    for (int i = 0; i < 5; ++i) out[i] = data[i];
    return ESP_OK;
}

static void log_dht_parsed(const uint8_t d[5])
{
    // Try DHT22/AM2302 parse
    int hum22 = ((int)d[0] << 8) | d[1];          // 0..1000 -> 0.1%
    int tmp22 = ((int)d[2] << 8) | d[3];
    bool neg = false;
    if (tmp22 & 0x8000) { neg = true; tmp22 &= 0x7FFF; }

    // Try DHT11 parse (integers only)
    int hum11 = d[0];
    int tmp11 = d[2];

    ESP_LOGI("DHT", "raw=%02X %02X %02X %02X %02X | DHT22: H=%d.%d%% T=%s%d.%dC | DHT11: H=%d%% T=%dC",
             d[0], d[1], d[2], d[3], d[4],
             hum22/10, hum22%10,
             neg?"-":"", tmp22/10, tmp22%10,
             hum11, tmp11);
}

static void dht_task(void *arg)
{
    // Warm-up after power-on
    vTaskDelay(pdMS_TO_TICKS(2000));
    while (1) {
        float hum = 0, temp = 0;
        esp_err_t err = dht_read_float_data(DHT_TYPE_DHT22, APP_DHT_GPIO, &hum, &temp);
        if (err != ESP_OK) {
            // fallback try DHT11
            err = dht_read_float_data(DHT_TYPE_DHT11, APP_DHT_GPIO, &hum, &temp);
        }
        if (err == ESP_OK) {
            ESP_LOGI("DHT", "H=%.1f%% T=%.1fC", hum, temp);
        } else {
            ESP_LOGW("DHT", "read error: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void start_dht_on_gpio39(void)
{
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreate(dht_task, "dht39", 4096, NULL, 3, NULL);
}

// ===== SSR blink on GPIO40 =====
static void ssr_blink_task(void *arg)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << APP_SSR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_drive_capability(APP_SSR_GPIO, GPIO_DRIVE_CAP_0);
    int lvl = 1;
    gpio_set_level(APP_SSR_GPIO, lvl);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        lvl ^= 1;
        gpio_set_level(APP_SSR_GPIO, lvl);
    }
}

void start_ssr40_blink(void)
{
    static bool started = false;
    if (started) return;
    started = true;
    // xTaskCreate(ssr_blink_task, "ssr40", 2048, NULL, 2, NULL);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << APP_SSR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_drive_capability(APP_SSR_GPIO, GPIO_DRIVE_CAP_0);
    int lvl = 1;
    gpio_set_level(APP_SSR_GPIO, lvl);
}
