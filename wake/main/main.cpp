#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#define TAG "WAKE"
#define s3
#ifdef s2
#define I2S_BCK_IO GPIO_NUM_26
#define I2S_WS_IO GPIO_NUM_25
#define I2S_SD_IO GPIO_NUM_22
#endif
#ifdef s3
#define I2S_BCK_IO (gpio_num_t)4
#define I2S_WS_IO  (gpio_num_t)5
#define I2S_SD_IO  (gpio_num_t)6
#endif
static i2s_chan_handle_t rx_handle;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;

void i2s_init()
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 128,
    };
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

    while (task_flag)
    {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, buffer, chunk * ch * sizeof(int16_t), &bytes_read, portMAX_DELAY);
        if (ret == ESP_OK && bytes_read == chunk * ch * sizeof(int16_t))
        {
            afe_handle->feed(afe_data, buffer);
        }
    }

    free(buffer);
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    int chunk = afe_handle->get_fetch_chunksize(afe_data);
    int16_t *buffer = (int16_t *)malloc(chunk * sizeof(int16_t));
    assert(buffer);

    ESP_LOGI(TAG, "Listening for wake word...");

    while (task_flag)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL)
        {
            ESP_LOGE(TAG, "AFE fetch failed");
            break;
        }

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED ***");
            ESP_LOGI(TAG, "Model index: %d, Word index: %d", res->wakenet_model_index, res->wake_word_index);
        }
    }

    free(buffer);
    vTaskDelete(NULL);
}

extern "C" void app_main()
{ // Check if PSRAM is available

    ESP_LOGI(TAG, "Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    // Get total and free size of PSRAM
    size_t psram_size = 22;
    ESP_LOGI(TAG, "PSRAM detected, total size: %d bytes", psram_size);
    // --- Heap Information ---
    // Log heap information BEFORE allocation
    ESP_LOGI(TAG, "--- Heap status before allocation ---");
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

    // Allocate a large buffer in PSRAM
    // Let's try to allocate 1MB (1024 * 1024 bytes)
    size_t buffer_size = 1 * 1024 * 1024;
    ESP_LOGI(TAG, "Attempting to allocate %d bytes from PSRAM...", buffer_size);

    // Use heap_caps_malloc with the MALLOC_CAP_SPIRAM flag
    char *psram_buffer = (char *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);

    if (psram_buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory from PSRAM!");
        // Log heap info again to see the state after failed allocation
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    }
    else
    {
        ESP_LOGI(TAG, "Successfully allocated %d bytes at address %p", buffer_size, psram_buffer);

        // --- Heap Information After Allocation ---
        ESP_LOGI(TAG, "--- Heap status after allocation ---");
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

        // Let's test the allocated memory
        ESP_LOGI(TAG, "Testing PSRAM buffer by writing and reading...");
        // Fill the buffer with a pattern
        for (size_t i = 0; i < buffer_size; i++)
        {
            psram_buffer[i] = (char)(i % 256);
        }

        // Verify the pattern
        bool success = true;
        for (size_t i = 0; i < buffer_size; i++)
        {
            if (psram_buffer[i] != (char)(i % 256))
            {
                ESP_LOGE(TAG, "Verification failed at index %d!", i);
                success = false;
                break;
            }
        }

        if (success)
        {
            ESP_LOGI(TAG, "PSRAM buffer test passed!");
        }
        else
        {
            ESP_LOGE(TAG, "PSRAM buffer test failed!");
        }

        // Free the allocated memory when done
        heap_caps_free(psram_buffer);
        ESP_LOGI(TAG, "Freed PSRAM buffer.");

        // --- Heap Information After Freeing ---
        ESP_LOGI(TAG, "--- Heap status after freeing memory ---");
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    }
    //=======================================================================================
    ESP_LOGI(TAG, "--- PSRAM Example End ---");
    ESP_LOGI(TAG, "Initializing I2S...");
    i2s_init();
    ESP_LOGI(TAG, "Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Loading models...");
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models || models->num == 0)
    {
        ESP_LOGE(TAG, "No models found in flash!");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    const char *input_fmt = "M"; // Single mic input
    char *model_name = NULL;
    for (int i = 0; i < models->num; i++)
    {
        if (strstr(models->model_name[i], "wn9_hilexin") || strstr(models->model_name[i], "wn9_alexa"))
        {
            model_name = models->model_name[i];
            break;
        }
    }
    afe_config_t *afe_config = afe_config_init(input_fmt, models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config)
    {
        ESP_LOGE(TAG, "Failed to init AFE config");
        return;
    }

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    task_flag = 1;
    xTaskCreatePinnedToCore(feed_Task, "feed", 2048, (void *)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(detect_Task, "detect", 2048, (void *)afe_data, 5, NULL, 1);
}
