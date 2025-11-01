/* debug_wake.cpp - improved/debugging version of your wake example */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
#include "esp_afe_sr_models.h"

#define TAG "WAKE_DBG"
#define s3
#ifdef s3
#define I2S_BCK_IO (gpio_num_t)4
#define I2S_WS_IO (gpio_num_t)5
#define I2S_SD_IO (gpio_num_t)6
#endif

static i2s_chan_handle_t rx_handle;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;

/* init I2S (same as your code, but keep DMA smaller while debugging if you want) */
void i2s_init()
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
        .gpio_cfg = {
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .din = I2S_SD_IO,
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // or RIGHT

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

/* helper: compute RMS of a PCM buffer */
static float compute_rms(const int16_t *buf, size_t samples)
{
    if (!buf || samples == 0)
        return 0.0f;
    uint64_t acc = 0;
    for (size_t i = 0; i < samples; ++i)
    {
        int32_t v = buf[i];
        acc += (uint64_t)(v * (int64_t)v);
    }
    double mean = ((double)acc) / (double)samples;
    return (float)sqrt(mean);
}

/* feed task: read from i2s, compute RMS + optionally print first samples, feed to AFE */
void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    if (!afe_handle || !afe_data)
    {
        ESP_LOGE(TAG, "afe_handle or afe_data NULL in feed_Task!");
        vTaskDelete(NULL);
        return;
    }

    int chunk = afe_handle->get_feed_chunksize(afe_data);
    int ch = afe_handle->get_feed_channel_num(afe_data);
    ESP_LOGI(TAG, "Feed task chunk=%d channels=%d", chunk, ch);

    size_t samples = (size_t)chunk * (size_t)ch;
    /* prefer PSRAM for large audio buffers if available */
    int16_t *buffer = (int16_t *)heap_caps_malloc(samples * sizeof(int16_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM);
    if (!buffer)
    {
        ESP_LOGW(TAG, "PSRAM allocation failed for audio buffer, falling back to heap_malloc");
        buffer = (int16_t *)malloc(samples * sizeof(int16_t));
    }
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Feed task started");

    while (task_flag)
    {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, buffer, samples * sizeof(int16_t), &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "i2s read error: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (bytes_read == 0)
        {
            ESP_LOGW(TAG, "i2s read returned 0 bytes");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* compute RMS and log first few samples for debugging */
        size_t got_samples = bytes_read / sizeof(int16_t);
        float rms = compute_rms(buffer, got_samples);
        ESP_LOGD(TAG, "I2S read %d bytes (%d samples), RMS=%.2f", (int)bytes_read, (int)got_samples, rms);

        /* print first 8 samples occasionally to check levels/format */
        static int print_count = 0;
        if ((print_count++ % 50) == 0)
        {
            ESP_LOGI(TAG, "I2S read %d bytes (%d samples), RMS=%.2f", (int)bytes_read, (int)got_samples, rms);

            ESP_LOGI(TAG, "samples[0..7]: %d,%d,%d,%d,%d,%d,%d,%d",
                     buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
        }

        /* if RMS is near zero it's likely the mic/pins are wrong or muted */
        if (rms < 2.0f)
        { // threshold tuned experimentally
            ESP_LOGW(TAG, "Low RMS (%.2f) - microphone may be silent or too quiet", rms);
        }

        /* feed into AFE */
        afe_handle->feed(afe_data, buffer);
    }

    heap_caps_free(buffer);
    vTaskDelete(NULL);
}

/* detect task: call afe fetch and react to wake events */
void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    if (!afe_handle || !afe_data)
    {
        ESP_LOGE(TAG, "afe_handle or afe_data NULL in detect_Task!");
        vTaskDelete(NULL);
        return;
    }

    int chunk = afe_handle->get_fetch_chunksize(afe_data);
    ESP_LOGI(TAG, "Detect task chunk=%d", chunk);

    ESP_LOGI(TAG, "Listening for wake word...");
    while (task_flag)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res)
        {
            ESP_LOGE(TAG, "AFE fetch returned NULL");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (res->ret_value == ESP_FAIL)
        {
            ESP_LOGE(TAG, "AFE fetch failed");
            break;
        }

        /* debug print of vad/wakenet state */
        ESP_LOGD(TAG, "AFE fetch: vad=%d, wakeup_state=%d, model_idx=%d, word_idx=%d",
                 res->vad_state, res->wakeup_state, res->wakenet_model_index, res->wake_word_index);

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED ***");
            ESP_LOGI(TAG, "Model index: %d, Word index: %d", res->wakenet_model_index, res->wake_word_index);
            /* take any action here (e.g., flash LED, start ASR, etc.) */
        }
    }
    vTaskDelete(NULL);
}

extern "C" void app_main()
{
    /* set verbose debug for relevant modules */
    esp_log_level_set("*", ESP_LOG_WARN); /* default */
    esp_log_level_set("WAKENET", ESP_LOG_DEBUG);
    esp_log_level_set("AFE", ESP_LOG_DEBUG);
    esp_log_level_set("WAKE_DBG", ESP_LOG_DEBUG);
    esp_log_level_set("WAKENET_DETECT", ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "Free PSRAM: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

    /* quick PSRAM allocation test (optional) */
    size_t buffer_size = 1 * 1024 * 1024;
    void *psram_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    if (psram_buffer)
    {
        ESP_LOGI(TAG, "PSRAM OK: allocated %u bytes at %p", (unsigned)buffer_size, psram_buffer);
        heap_caps_free(psram_buffer);
    }
    else
    {
        ESP_LOGW(TAG, "PSRAM allocation failed (no PSRAM or not configured)");
    }

    ESP_LOGI(TAG, "Initializing I2S...");
    i2s_init();

    ESP_LOGI(TAG, "Loading models...");
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models || models->num == 0)
    {
        ESP_LOGE(TAG, "No models found in flash!");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        return;
    }

    ESP_LOGI(TAG, "Found %d model(s). Listing:", models->num);
    for (int i = 0; i < models->num; ++i)
    {
        ESP_LOGI(TAG, "  [%d] %s", i, models->model_name[i] ? models->model_name[i] : "(null)");
    }

    const char *input_fmt = "M"; // single mic
    /* optionally pick a specific model here: see models->model_name[] */
    // const char *desired = "wn9_hilexin";
    // if you want to force change, you may need to edit AFE config after creation - ensure model present

    afe_config_t *afe_config = afe_config_init(input_fmt, models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config)
    {
        ESP_LOGE(TAG, "Failed to init AFE config");
        return;
    }

    afe_handle = esp_afe_handle_from_config(afe_config);
    if (!afe_handle)
    {
        ESP_LOGE(TAG, "Failed to get afe_handle from config");
        afe_config_free(afe_config);
        return;
    }
    // esp_afe_sr_set_wakenet_sensitivity(afe_handle, 0.7f);

    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    if (!afe_data)
    {
        ESP_LOGE(TAG, "Failed to create afe_data");
        afe_config_free(afe_config);
        return;
    }
    // modify wakenet detection threshold
    // Select model by index (0 = wn9_alexa)
    // esp_afe_sr_set_wakenet(afe_handle, afe_data, 0);

    // Lower sensitivity = easier detection (0.0 = hard, 1.0 = easy)
    // esp_afe_sr_set_wakenet_sensitivity(afe_handle, 0.7f); // 0.7 = balanced
    // afe_handle->set_wakenet_threshold(afe_data, 2, 0.6); // set model2's threshold to 0.6
    // afe_handle->reset_wakenet_threshold(afe_data, 1);    // reset model1's threshold to default
    // afe_handle->reset_wakenet_threshold(afe_data, 2);    // reset model2's threshold to default
    afe_config_free(afe_config);

    task_flag = 1;
    /* bigger stacks for audio threads */
    xTaskCreatePinnedToCore(feed_Task, "feed", 4096, (void *)afe_data, 6, NULL, 0);
    xTaskCreatePinnedToCore(detect_Task, "detect", 4096, (void *)afe_data, 6, NULL, 1);

    /* app_main returns; main_task will continue */
}
