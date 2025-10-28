#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "dl_lib_coefgetter_if.h"
#include "string.h"

#define TAG "WAKE"
#define I2S_BCK_IO (gpio_num_t)26
#define I2S_WS_IO (gpio_num_t)25
#define I2S_SD_IO (gpio_num_t)22

static i2s_chan_handle_t rx_handle;

static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, NULL, &rx_handle);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .din = I2S_SD_IO,
        },
    };
    i2s_channel_init_std_mode(rx_handle, &std_cfg);
    i2s_channel_enable(rx_handle);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Initializing I2S...");
    i2s_init();

    srmodel_list_t *models = esp_srmodel_init(NULL);
    if (!models || models->num == 0)
    {
        ESP_LOGE(TAG, "No models! Enable 'Hi, Lexin (wn9_hilexin)' in menuconfig");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    ESP_LOGI(TAG, "Found %d model(s):", models->num);

    char *model_name = NULL;
    for (int i = 0; i < models->num; i++)
    {
        if (models->model_name[i] && strstr(models->model_name[i], "wn9_hilexin"))
        {
            model_name = models->model_name[i];
            ESP_LOGI(TAG, "  [x] Using: '%s'", model_name);
            break;
        }
        else if (models->model_name[i])
        {
            ESP_LOGI(TAG, "  [ ] Skip: '%s'", i, models->model_name[i]);
        }
    }

    if (!model_name)
    {
        ESP_LOGE(TAG, "No WN9 HiLexin model! Check menuconfig.");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    ESP_LOGI(TAG, "Loading model: %s", model_name);

    esp_wn_iface_t *wakenet = (esp_wn_iface_t *)esp_wn_handle_from_name(model_name);
    if (!wakenet)
    {
        ESP_LOGE(TAG, "No wakenet interface!");
        esp_restart();
    }

    model_iface_data_t *model_data = wakenet->create(model_name, DET_MODE_95);
    if (!model_data)
    {
        ESP_LOGE(TAG, "create() failed!");
        esp_restart();
    }

    int chunk_size = wakenet->get_samp_chunksize(model_data);
    int16_t *buffer = (int16_t *)malloc(chunk_size * sizeof(int16_t));
    if (!buffer)
    {
        ESP_LOGE(TAG, "malloc failed!");
        wakenet->destroy(model_data);
        esp_restart();
    }

    ESP_LOGI(TAG, "Listening for 'Hi, Lexin'...");

    while (true)
    {
        size_t bytes_read = 0;
        esp_err_t r = i2s_channel_read(rx_handle, buffer, chunk_size * 2, &bytes_read, portMAX_DELAY);
        if (r == ESP_OK && bytes_read == chunk_size * 2)
        {
            if (wakenet->detect(model_data, buffer) == WAKENET_DETECTED)
            {
                ESP_LOGI(TAG, "*** WAKE WORD DETECTED! ***");
            }
        }
    }
}