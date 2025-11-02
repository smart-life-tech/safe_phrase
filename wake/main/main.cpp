#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"

#define TAG "WAKE_MN"
#define I2S_BCK_IO 4
#define I2S_WS_IO 5
#define I2S_SD_IO 6

static i2s_chan_handle_t rx_handle;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int running = 1;

/* Initialize I2S for single microphone input */
void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 256,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.bclk = I2S_BCK_IO, .ws = I2S_WS_IO, .din = I2S_SD_IO},
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

/* Compute RMS for debugging mic input */
float compute_rms(const int16_t *buf, size_t samples)
{
    if (!buf || samples == 0)
        return 0.0f;
    double sum = 0;
    for (size_t i = 0; i < samples; i++)
        sum += (double)buf[i] * buf[i];
    return sqrt(sum / samples);
}

void feed_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    int chunk = afe_handle->get_feed_chunksize(afe_data);
    int ch = afe_handle->get_feed_channel_num(afe_data);
    size_t samples = chunk * ch;

    int16_t *buffer = (int16_t *)heap_caps_malloc(samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Feed task started, chunk=%d", chunk);

    while (running)
    {
        size_t bytes_read = 0;
        i2s_channel_read(rx_handle, buffer, samples * sizeof(int16_t), &bytes_read, portMAX_DELAY);
        afe_handle->feed(afe_data, buffer);
    }
    heap_caps_free(buffer);
    vTaskDelete(NULL);
}

void detect_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;

    // Initialize MultiNet model for speech recognition
    esp_mn_iface_t *mn_iface = esp_mn_handle_from_name("mn7_en"); // model name: "mn7_en" (English)
    model_iface_data_t *mn_data = mn_iface->create("mn7_en");

    ESP_LOGI(TAG, "Listening for wake words...");

    while (running)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res)
            continue;

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED *** Model: %d Word: %d", res->wakenet_model_index, res->wake_word_index);

            // After wakeup, capture a short segment for speech recognition
            ESP_LOGI(TAG, "Listening for command...");
            afe_handle->enable_wakenet(afe_data, false); // disable wakenet temporarily

            for (int i = 0; i < 50; i++) // capture ~3 sec
            {
                afe_fetch_result_t *cmd_res = afe_handle->fetch(afe_data);
                if (!cmd_res)
                    continue;

                // feed into MultiNet recognizer
                int command_id = mn_iface->detect(mn_data, cmd_res->data);
                if (command_id >= 0)
                {
                    const char *command_str = mn_iface->get_word(mn_data, command_id);
                    ESP_LOGI(TAG, "✅ Recognized command: %s", command_str);

                    // Example: map greetings
                    if (strstr(command_str, "hello") || strstr(command_str, "hi"))
                        ESP_LOGI(TAG, "👋 Greeting detected!");
                    else if (strstr(command_str, "morning"))
                        ESP_LOGI(TAG, "🌅 Good morning!");
                    else if (strstr(command_str, "evening"))
                        ESP_LOGI(TAG, "🌇 Good evening!");

                    break;
                }
            }

            mn_iface->clear(mn_data);
            afe_handle->enable_wakenet(afe_data, true); // re-enable wakenet
        }
    }

    mn_iface->destroy(mn_data);
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Initializing I2S...");
    i2s_init();

    ESP_LOGI(TAG, "Loading AFE + WakeNet + MultiNet...");
    srmodel_list_t *models = esp_srmodel_init("model");
    ESP_LOGI(TAG, "Models found: %d", models->num);

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);

    running = 1;
    xTaskCreatePinnedToCore(feed_task, "feed_task", 4096, afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(detect_task, "detect_task", 8192, afe_data, 5, NULL, 1);
}
