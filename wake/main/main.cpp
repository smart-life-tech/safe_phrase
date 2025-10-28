#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"

int detect_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;

void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int feed_channel = afe_handle->get_feed_channel_num(afe_data);

    int16_t *i2s_buff = (int16_t *)malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);

    while (task_flag)
    {
        // Replace with your actual I2S read here
        // For now, simulate silence
        memset(i2s_buff, 0, audio_chunksize * sizeof(int16_t) * feed_channel);

        afe_handle->feed(afe_data, i2s_buff);
    }

    free(i2s_buff);
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    int16_t *buff = (int16_t *)malloc(afe_chunksize * sizeof(int16_t));
    assert(buff);

    printf("------------detect start------------\n");

    while (task_flag)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL)
        {
            printf("fetch error!\n");
            break;
        }

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            printf("wakeword detected\n");
            printf("model index:%d, word index:%d\n", res->wakenet_model_index, res->wake_word_index);
            printf("-----------LISTENING-----------\n");
        }
    }

    free(buff);
    vTaskDelete(NULL);
}

void app_main()
{
    // Load models from flash
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models)
    {
        for (int i = 0; i < models->num; i++)
        {
            if (strstr(models->model_name[i], ESP_WN_PREFIX))
            {
                printf("wakenet model in flash: %s\n", models->model_name[i]);
            }
        }
    }

    // Define input format manually (replaces esp_get_input_format())
    afe_input_format_t input_fmt = {
        .sample_rate = 16000,
        .channel_mask = AFE_CHANNEL_ANY,
        .bit_width = 16
    };

    afe_config_t *afe_config = afe_config_init(&input_fmt, models, AFE_TYPE_SR, AFE_MODE_LOW_COST);

    if (afe_config->wakenet_model_name)
        printf("wakeword model in AFE config: %s\n", afe_config->wakenet_model_name);
    if (afe_config->wakenet_model_name_2)
        printf("wakeword model in AFE config: %s\n", afe_config->wakenet_model_name_2);

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    task_flag = 1;
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void *)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(&detect_Task, "detect", 4 * 1024, (void *)afe_data, 5, NULL, 1);
}
