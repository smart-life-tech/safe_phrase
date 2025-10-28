#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"

#define TAG "WAKE"
#define I2S_BCK_IO GPIO_NUM_26
#define I2S_WS_IO GPIO_NUM_25
#define I2S_SD_IO GPIO_NUM_22

static i2s_chan_handle_t rx_handle;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;

void i2s_init()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .din = I2S_SD_IO,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    int chunk = afe_handle->get_feed_chunksize(afe_data);
    int ch = afe_handle->get_feed_channel_num(afe_data);
    int16_t *buffer = (int16_t *)malloc(chunk * ch * sizeof(int16_t));
    assert(buffer);

    while (task_flag) {
        size_t bytes_read = 0;
        if (i2s_channel_read(rx_handle, buffer, chunk * ch * 2, &bytes_read, portMAX_DELAY) == ESP_OK) {
            afe_handle->feed(afe_data, buffer);
        }
    }
    free(buffer);
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    ESP_LOGI(TAG, "Listening for 'Hi, Lexin'...");

    while (task_flag) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (res && res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED! ***");
        }
    }
    vTaskDelete(NULL);
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing I2S...");
    i2s_init();

    ESP_LOGI(TAG, "Loading models...");
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models || models->num == 0) {
        ESP_LOGE(TAG, "No models! Flash wn9_hilexin.packed.bin to 0x110000");
        esp_restart();
    }

    char *model_name = NULL;
    for (int i = 0; i < models->num; i++) {
        if (strstr(models->model_name[i], "wn9_hilexin")) {
            model_name = models->model_name[i];
            ESP_LOGI(TAG, "Using model: %s", model_name);
            break;
        }
    }
    if (!model_name) {
        ESP_LOGE(TAG, "wn9_hilexin not found!");
        esp_restart();
    }

    // Correct AFE config
    const char *input_fmt = "M";
    afe_config_t *afe_config = afe_config_init(input_fmt, model_name, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config) {
        ESP_LOGE(TAG, "AFE config failed");
        esp_restart();
    }

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    task_flag = 1;
    xTaskCreatePinnedToCore(feed_Task, "feed", 4096, afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(detect_Task, "detect", 4096, afe_data, 5, NULL, 1);

    vTaskDelay(portMAX_DELAY);
}