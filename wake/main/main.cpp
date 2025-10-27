#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_sr_models.h"

static const char *TAG = "WakeWord";

/* I2S configuration for INMP441 */
#define I2S_NUM         (I2S_NUM_0)
#define SAMPLE_RATE     (16000)
#define I2S_BCK_IO      26
#define I2S_WS_IO       25
#define I2S_SD_IO       22

static void i2s_init(void)
{
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_SD_IO,
        },
    };
    i2s_chan_handle_t rx_handle;
    i2s_new_channel(&(i2s_chan_config_t){
        .id = I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 240,
    }, NULL, &rx_handle);

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S initialized.");
}

/* WakeNet Task */
void wakenet_task(void *arg)
{
    esp_wn_iface_t *wakenet;
    model_iface_data_t *model_data;
    const esp_wn_iface_t *wn_iface = &esp_wn_handle;
    const esp_wn_model_t *wn_model = esp_wn_model_get(ESP_WN_MODEL_HI_LEXIN);

    wakenet = (esp_wn_iface_t *)wn_iface;
    model_data = wakenet->create((const model_coeff_getter_t *)wn_model, DET_MODE_95);

    int chunk_size = wakenet->get_samp_chunksize(model_data);
    int16_t *buffer = malloc(chunk_size * sizeof(int16_t));

    i2s_chan_handle_t rx_handle = NULL;
    i2s_channel_get_handle(I2S_NUM, &rx_handle);

    ESP_LOGI(TAG, "WakeNet started. Say \"Hi Lexin\"...");

    while (1) {
        size_t bytes_read = 0;
        i2s_channel_read(rx_handle, buffer, chunk_size * sizeof(int16_t), &bytes_read, portMAX_DELAY);

        if (bytes_read > 0) {
            wakenet_state_t state = wakenet->detect(model_data, buffer);
            if (state == WAKENET_DETECTED) {
                ESP_LOGI(TAG, "Wake word detected!");
            }
        }
    }

    wakenet->destroy(model_data);
    free(buffer);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Wake Word Detector...");
    i2s_init();
    xTaskCreatePinnedToCore(wakenet_task, "wakenet_task", 8192, NULL, 5, NULL, 1);
}
