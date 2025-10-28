#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "hilexin_wn5X3.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "dl_lib_coefgetter_if.h"
#include "model_path.h"
#include "string.h"
// #include "hilexin.h"

extern const esp_wn_iface_t esp_wn_handle;
extern const model_coeff_getter_t get_coeff_hilexin_wn5X3;

#define TAG "WAKE"
#define I2S_BCK_IO (gpio_num_t)26
#define I2S_WS_IO (gpio_num_t)25
#define I2S_SD_IO (gpio_num_t)22

static i2s_chan_handle_t rx_handle;

static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 240,
    };
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

    // srmodel_list_t *models = esp_srmodel_init("model");
    // ESP_LOGI(TAG, "loading models...");
    // char *model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "hilexin");
    // ESP_LOGI(TAG, "loaded lexin...");
    // esp_wn_iface_t *wakenet = (esp_wn_iface_t *)esp_wn_handle_from_name(model_name);
    // ESP_LOGI(TAG, "Initializing lexin...");
    // model_iface_data_t *model_data = wakenet->create(model_name, DET_MODE_95);
    // ESP_LOGI(TAG, "Initialized lexin...");

    // esp_wn_iface_t *wakenet = (esp_wn_iface_t *)&esp_wn_handle;
    // ESP_LOGI(TAG, "Initializing lexin...");
    // model_iface_data_t *model_data = wakenet->create(&get_coeff_hilexin_wn5X3, DET_MODE_95);
    // ESP_LOGI(TAG, "Initialized lexin...");

    srmodel_list_t *models = esp_srmodel_init(NULL); // NULL = use embedded models
    if (!models || models->num == 0)
    {
        ESP_LOGE(TAG, "No models found!");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    ESP_LOGI(TAG, "Found %d models:", models->num);
    // for (int i = 0; i < models->num; ++i)
    // {
    //     ESP_LOGI(TAG, "Model %d: %s", i, models->model[i]);
    // }
    // Option 1: Case-insensitive prefix
    char *model_name1 = esp_srmodel_filter(models, "HiLexin", NULL);
    if (!model_name1)
    {
        ESP_LOGE(TAG, "HiLexin model not found!");
        vTaskDelay(pdMS_TO_TICKS(5000));
        // esp_restart();
    }

    // Option 2: Full name match (if you see it in logs)
    char *model_name2 = esp_srmodel_filter(models, "HiLexin_WN5X3", NULL);
    if (!model_name2)
    {
        ESP_LOGE(TAG, "HiLexin_WN5X3 model not found!");
        vTaskDelay(pdMS_TO_TICKS(5000));
        // esp_restart();
    }

    // Option 3: Try lowercase
    char *model_name3 = esp_srmodel_filter(models, "hilexin", NULL);
    if (!model_name3)
    {
        ESP_LOGE(TAG, "hilexin model not found!");
        vTaskDelay(pdMS_TO_TICKS(5000));
        // esp_restart();
    }
    // Option 4: Use defined prefix constant
    char *model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "hilexin");
    if (!model_name)
    {
        ESP_LOGE(TAG, "hilexin model not found!");
        vTaskDelay(pdMS_TO_TICKS(5000));
        // esp_restart();
    }
    ESP_LOGI(TAG, "Found %d embedded model(s):", models->num);
    for (int i = 0; i < models->num; i++)
    {
        ESP_LOGI(TAG, "  [%d] '%s'", i, models->model_name[i]);
    }
    ESP_LOGI(TAG, "Using model: '%s'", model_name); // ← This should now print!

    esp_wn_iface_t *wakenet = (esp_wn_iface_t *)esp_wn_handle_from_name(model_name);
    if (!wakenet)
    {
        ESP_LOGE(TAG, "No interface for %s", model_name);
        vTaskDelay(pdMS_TO_TICKS(5000));
        // esp_restart();
    }

    model_iface_data_t *model_data = wakenet->create(model_name, DET_MODE_95);
    if (!model_data)
    {
        ESP_LOGE(TAG, "create() failed for %s", model_name);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    int chunk_size = wakenet->get_samp_chunksize(model_data);
    int16_t *buffer = (int16_t *)malloc(chunk_size * sizeof(int16_t));
    if (!buffer)
    {
        ESP_LOGE(TAG, "malloc failed");
        wakenet->destroy(model_data);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    ESP_LOGI(TAG, "Listening for 'Hi, Lexin'...");
    // int chunk_size = wakenet->get_samp_chunksize(model_data);
    // int16_t *buffer = (int16_t *)malloc(chunk_size * sizeof(int16_t));

    while (true)
    {
        size_t bytes_read = 0;
        i2s_channel_read(rx_handle, buffer, chunk_size * sizeof(int16_t), &bytes_read, portMAX_DELAY);
        if (bytes_read > 0)
        {
            wakenet_state_t state = wakenet->detect(model_data, buffer);
            if (state == WAKENET_DETECTED)
            {
                ESP_LOGI(TAG, "Wake word detected!");
            }
        }
    }
}
