#include "esp_log.h"
#include "esp_system.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_sr_iface.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#define I2S_PORT I2S_NUM_0
#define TAG "SafePhraseWake"

#define I2S_WS   GPIO_NUM_25
#define I2S_SCK  GPIO_NUM_26
#define I2S_SD   GPIO_NUM_22
#define LED_PIN  GPIO_NUM_2

extern const esp_sr_iface_t *model;
extern const esp_sr_wakenet_ops_t *wakenet;

void init_led() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_PIN);
    gpio_config(&io_conf);
    gpio_set_level(LED_PIN, 0);
}

void init_i2s() {
    i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SCK,
            .ws = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_SD,
        },
    };
    i2s_new_channel(&i2s_config, NULL, NULL);
    ESP_LOGI(TAG, "I2S initialized");
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "==== SafePhrase Wake ====");
    init_led();
    init_i2s();

    esp_sr_wakenet_model_t *wn_handle = esp_sr_wakenet_create(WN9_Q8);
    if (!wn_handle) {
        ESP_LOGE(TAG, "WakeNet init failed!");
        return;
    }

    ESP_LOGI(TAG, "Wake word model loaded");

    int16_t buffer[512];
    size_t bytes_read;
    while (true) {
        i2s_read(I2S_PORT, buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);
        if (bytes_read > 0) {
            int result = esp_sr_wakenet_detect(wn_handle, buffer);
            if (result) {
                ESP_LOGI(TAG, " Wake word detected!");
                gpio_set_level(LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(1000));
                gpio_set_level(LED_PIN, 0);
            }
        }
    }
}
