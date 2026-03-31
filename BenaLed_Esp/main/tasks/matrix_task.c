#include "matrix_task.h"

typedef struct
{
    uint8_t rgb[MATRIX_RGB_FRAME_SIZE];
} matrix_frame_t;

static const char *TAG = "matrix_task";

static QueueHandle_t s_matrix_queue = NULL;
static matrix_frame_t s_consumer_frame;
static matrix_frame_t s_drop_frame;
static uint8_t s_front_buffer_storage[MATRIX_RGB_FRAME_SIZE];
static uint8_t s_back_buffer_storage[MATRIX_RGB_FRAME_SIZE];
static uint8_t *s_front_buffer = s_front_buffer_storage;
static uint8_t *s_back_buffer = s_back_buffer_storage;
static uint32_t s_frame_counter = 0;
static portMUX_TYPE s_matrix_mux = portMUX_INITIALIZER_UNLOCKED;

static void log_matrix_summary(const uint8_t *rgb, uint32_t frame_id)
{
    if (rgb == NULL)
    {
        return;
    }

    uint32_t sum_r = 0;
    uint32_t sum_g = 0;
    uint32_t sum_b = 0;
    uint32_t checksum = 2166136261u;

    for (size_t i = 0; i < MATRIX_RGB_FRAME_SIZE; i += MATRIX_CHANNELS)
    {
        uint8_t r = rgb[i + 0];
        uint8_t g = rgb[i + 1];
        uint8_t b = rgb[i + 2];

        sum_r += r;
        sum_g += g;
        sum_b += b;

        checksum ^= r;
        checksum *= 16777619u;
        checksum ^= g;
        checksum *= 16777619u;
        checksum ^= b;
        checksum *= 16777619u;
    }

    const uint32_t pixels = MATRIX_WIDTH * MATRIX_HEIGHT;
    const uint8_t r0 = rgb[0];
    const uint8_t g0 = rgb[1];
    const uint8_t b0 = rgb[2];

    ESP_LOGI(TAG,
             "Frame %lu resumo: p0=#%02X%02X%02X avg=(%lu,%lu,%lu) fnv1a=0x%08lX",
             (unsigned long)frame_id,
             r0, g0, b0,
             (unsigned long)(sum_r / pixels),
             (unsigned long)(sum_g / pixels),
             (unsigned long)(sum_b / pixels),
             (unsigned long)checksum);
}

static void matrix_consumer_task(void *arg)
{
    while (1)
    {
        if (xQueueReceive(s_matrix_queue, &s_consumer_frame, portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        taskENTER_CRITICAL(&s_matrix_mux);
        memcpy(s_back_buffer, s_consumer_frame.rgb, MATRIX_RGB_FRAME_SIZE);

        uint8_t *tmp = s_front_buffer;
        s_front_buffer = s_back_buffer;
        s_back_buffer = tmp;
        s_frame_counter++;
        uint32_t frame_id = s_frame_counter;
        taskEXIT_CRITICAL(&s_matrix_mux);

        log_matrix_summary(s_front_buffer, frame_id);
    }
}

esp_err_t init_matrix_task(void)
{
    if (s_matrix_queue != NULL)
    {
        return ESP_OK;
    }

    s_matrix_queue = xQueueCreate(MATRIX_QUEUE_LEN, sizeof(matrix_frame_t));
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

    (void)xQueueReceive(s_matrix_queue, &s_drop_frame, 0);

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