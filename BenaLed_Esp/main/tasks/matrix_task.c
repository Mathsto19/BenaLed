#include "matrix_task.h"

static const char *TAG = "matrix_task";

static QueueHandle_t s_matrix_queue = NULL;
static uint8_t s_drop_frame_storage[MATRIX_RGB_FRAME_SIZE];
static uint8_t s_front_buffer_storage[MATRIX_RGB_FRAME_SIZE];
static uint8_t s_back_buffer_storage[MATRIX_RGB_FRAME_SIZE];
static uint8_t *s_front_buffer = s_front_buffer_storage;
static uint8_t *s_back_buffer = s_back_buffer_storage;
static uint32_t s_frame_counter = 0;
static TickType_t s_fps_window_start_tick = 0;
static uint32_t s_fps_window_frames = 0;
static portMUX_TYPE s_matrix_mux = portMUX_INITIALIZER_UNLOCKED;

static void matrix_consumer_task(void *arg)
{
    s_fps_window_start_tick = xTaskGetTickCount();

    while (1)
    {
        if (xQueueReceive(s_matrix_queue, s_back_buffer, portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        taskENTER_CRITICAL(&s_matrix_mux);
        uint8_t *tmp = s_front_buffer;
        s_front_buffer = s_back_buffer;
        s_back_buffer = tmp;
        s_frame_counter++;
        taskEXIT_CRITICAL(&s_matrix_mux);

        s_fps_window_frames++;
        TickType_t now_tick = xTaskGetTickCount();
        TickType_t elapsed_tick = now_tick - s_fps_window_start_tick;
        if (elapsed_tick >= pdMS_TO_TICKS(1000))
        {
            float elapsed_ms = (float)elapsed_tick * (float)portTICK_PERIOD_MS;
            float fps = (elapsed_ms > 0.0f) ? ((float)s_fps_window_frames * 1000.0f) / elapsed_ms : 0.0f;
            ESP_LOGI(TAG, "FPS: %.2f", (double)fps);
            s_fps_window_start_tick = now_tick;
            s_fps_window_frames = 0;
        }
    }
}

esp_err_t init_matrix_task(void)
{
    if (s_matrix_queue != NULL)
    {
        return ESP_OK;
    }

    s_matrix_queue = xQueueCreate(MATRIX_QUEUE_LEN, MATRIX_RGB_FRAME_SIZE);
    if (s_matrix_queue == NULL)
    {
        ESP_LOGE(TAG, "Falha ao criar fila de matriz");
        return ESP_ERR_NO_MEM;
    }

    memset(s_front_buffer_storage, 0, sizeof(s_front_buffer_storage));
    memset(s_back_buffer_storage, 0, sizeof(s_back_buffer_storage));

    BaseType_t created = xTaskCreate(matrix_consumer_task, "matrix_consumer_task", 6144, NULL, 6, NULL);
    if (created != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar task de matriz");
        vQueueDelete(s_matrix_queue);
        s_matrix_queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Task de matriz iniciada (queue=%d, frame=%d bytes)", MATRIX_QUEUE_LEN, MATRIX_RGB_FRAME_SIZE);
    return ESP_OK;
}

esp_err_t matrix_queue_push(const uint8_t *rgb_frame, size_t len)
{
    if (rgb_frame == NULL || len != MATRIX_RGB_FRAME_SIZE || s_matrix_queue == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (xQueueSend(s_matrix_queue, rgb_frame, 0) == pdPASS)
    {
        return ESP_OK;
    }

    (void)xQueueReceive(s_matrix_queue, s_drop_frame_storage, 0);

    if (xQueueSend(s_matrix_queue, rgb_frame, 0) != pdPASS)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

const uint8_t *matrix_get_front_buffer(void)
{
    const uint8_t *front;

    taskENTER_CRITICAL(&s_matrix_mux);
    front = s_front_buffer;
    taskEXIT_CRITICAL(&s_matrix_mux);

    return front;
}

size_t matrix_get_frame_size(void)
{
    return MATRIX_RGB_FRAME_SIZE;
}

uint32_t matrix_get_frame_counter(void)
{
    uint32_t counter;

    taskENTER_CRITICAL(&s_matrix_mux);
    counter = s_frame_counter;
    taskEXIT_CRITICAL(&s_matrix_mux);

    return counter;
}