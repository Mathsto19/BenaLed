#include "matrix_task.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "matrix_task";

#define RMT_RESOLUTION_HZ (10 * 1000 * 1000) // 10 MHz
#define RMT_QUEUE_DEPTH 8
#define RMT_TIMEOUT_MS 1000

// WS2812B timing (10 MHz clock = 100 ns/tick)
#define WS2812B_T0H 4 // ~400 ns
#define WS2812B_T0L 8 // ~800 ns
#define WS2812B_T1H 7 // ~700 ns
#define WS2812B_T1L 6 // ~600 ns

#if (MATRIX_COLOR_ORDER < MATRIX_COLOR_ORDER_RGB) || (MATRIX_COLOR_ORDER > MATRIX_COLOR_ORDER_BGR)
#error "MATRIX_COLOR_ORDER invalido em app_config.h"
#endif

#if (MATRIX_LAYOUT != MATRIX_LAYOUT_PROGRESSIVE) && (MATRIX_LAYOUT != MATRIX_LAYOUT_SERPENTINE)
#error "MATRIX_LAYOUT invalido em app_config.h"
#endif

#if (MATRIX_ORIGIN < MATRIX_ORIGIN_TOP_LEFT) || (MATRIX_ORIGIN > MATRIX_ORIGIN_BOTTOM_RIGHT)
#error "MATRIX_ORIGIN invalido em app_config.h"
#endif

static QueueHandle_t s_matrix_queue = NULL;
static uint8_t s_drop_frame_storage[MATRIX_RGB_FRAME_SIZE];
static uint8_t s_front_buffer_storage[MATRIX_RGB_FRAME_SIZE];
static uint8_t s_back_buffer_storage[MATRIX_RGB_FRAME_SIZE];
static uint8_t *s_front_buffer = s_front_buffer_storage;
static uint8_t *s_back_buffer = s_back_buffer_storage;
static uint8_t s_tx_buffer[MATRIX_RGB_FRAME_SIZE];
static uint32_t s_frame_counter = 0;
static TickType_t s_fps_window_start_tick = 0;
static uint32_t s_fps_window_frames = 0;
static portMUX_TYPE s_matrix_mux = portMUX_INITIALIZER_UNLOCKED;

static rmt_channel_handle_t s_rmt_channel = NULL;
static rmt_encoder_handle_t s_rmt_encoder = NULL;
static bool s_rmt_initialized = false;
static bool s_rmt_with_dma = false;

static const char *matrix_color_order_to_str(void)
{
    switch (MATRIX_COLOR_ORDER)
    {
    case MATRIX_COLOR_ORDER_RGB:
        return "RGB";
    case MATRIX_COLOR_ORDER_GRB:
        return "GRB";
    case MATRIX_COLOR_ORDER_BRG:
        return "BRG";
    case MATRIX_COLOR_ORDER_RBG:
        return "RBG";
    case MATRIX_COLOR_ORDER_GBR:
        return "GBR";
    case MATRIX_COLOR_ORDER_BGR:
        return "BGR";
    default:
        return "UNKNOWN";
    }
}

static const char *matrix_layout_to_str(void)
{
    return (MATRIX_LAYOUT == MATRIX_LAYOUT_SERPENTINE) ? "SERPENTINE" : "PROGRESSIVE";
}

static const char *matrix_origin_to_str(void)
{
    switch (MATRIX_ORIGIN)
    {
    case MATRIX_ORIGIN_TOP_LEFT:
        return "TOP_LEFT";
    case MATRIX_ORIGIN_TOP_RIGHT:
        return "TOP_RIGHT";
    case MATRIX_ORIGIN_BOTTOM_LEFT:
        return "BOTTOM_LEFT";
    case MATRIX_ORIGIN_BOTTOM_RIGHT:
        return "BOTTOM_RIGHT";
    default:
        return "UNKNOWN";
    }
}

static void matrix_apply_origin(size_t logical_x, size_t logical_y, size_t *mapped_x, size_t *mapped_y)
{
    size_t x = logical_x;
    size_t y = logical_y;

    switch (MATRIX_ORIGIN)
    {
    case MATRIX_ORIGIN_TOP_LEFT:
        break;
    case MATRIX_ORIGIN_TOP_RIGHT:
        x = (size_t)(MATRIX_WIDTH - 1U) - x;
        break;
    case MATRIX_ORIGIN_BOTTOM_LEFT:
        y = (size_t)(MATRIX_HEIGHT - 1U) - y;
        break;
    case MATRIX_ORIGIN_BOTTOM_RIGHT:
        x = (size_t)(MATRIX_WIDTH - 1U) - x;
        y = (size_t)(MATRIX_HEIGHT - 1U) - y;
        break;
    default:
        break;
    }

    *mapped_x = x;
    *mapped_y = y;
}

static size_t matrix_logical_xy_to_physical_pixel_index(size_t logical_x, size_t logical_y)
{
    size_t x = logical_x;
    size_t y = logical_y;
    matrix_apply_origin(logical_x, logical_y, &x, &y);

    if (MATRIX_LAYOUT == MATRIX_LAYOUT_SERPENTINE)
    {
        bool reverse_row = ((y & 1U) != 0U);
#if MATRIX_SERPENTINE_ODD_ROWS_REVERSED == 0
        reverse_row = !reverse_row;
#endif
        if (reverse_row)
        {
            x = (size_t)(MATRIX_WIDTH - 1U) - x;
        }
    }

    return (y * (size_t)MATRIX_WIDTH) + x;
}

static void matrix_reorder_pixel_rgb(const uint8_t *rgb, uint8_t *ordered)
{
    const uint8_t r = rgb[0];
    const uint8_t g = rgb[1];
    const uint8_t b = rgb[2];

    switch (MATRIX_COLOR_ORDER)
    {
    case MATRIX_COLOR_ORDER_RGB:
        ordered[0] = r;
        ordered[1] = g;
        ordered[2] = b;
        break;
    case MATRIX_COLOR_ORDER_GRB:
        ordered[0] = g;
        ordered[1] = r;
        ordered[2] = b;
        break;
    case MATRIX_COLOR_ORDER_BRG:
        ordered[0] = b;
        ordered[1] = r;
        ordered[2] = g;
        break;
    case MATRIX_COLOR_ORDER_RBG:
        ordered[0] = r;
        ordered[1] = b;
        ordered[2] = g;
        break;
    case MATRIX_COLOR_ORDER_GBR:
        ordered[0] = g;
        ordered[1] = b;
        ordered[2] = r;
        break;
    case MATRIX_COLOR_ORDER_BGR:
        ordered[0] = b;
        ordered[1] = g;
        ordered[2] = r;
        break;
    default:
        ordered[0] = g;
        ordered[1] = r;
        ordered[2] = b;
        break;
    }
}

static void matrix_prepare_tx_buffer(const uint8_t *rgb_frame, uint8_t *tx_buffer, size_t size)
{
    if (size != MATRIX_RGB_FRAME_SIZE)
    {
        return;
    }

    for (size_t y = 0; y < (size_t)MATRIX_HEIGHT; y++)
    {
        for (size_t x = 0; x < (size_t)MATRIX_WIDTH; x++)
        {
            size_t logical_pixel_index = (y * (size_t)MATRIX_WIDTH) + x;
            size_t physical_pixel_index = matrix_logical_xy_to_physical_pixel_index(x, y);

            size_t logical_offset = logical_pixel_index * (size_t)MATRIX_CHANNELS;
            size_t physical_offset = physical_pixel_index * (size_t)MATRIX_CHANNELS;

            matrix_reorder_pixel_rgb(&rgb_frame[logical_offset], &tx_buffer[physical_offset]);
        }
    }
}

static esp_err_t rmt_matrix_init(void)
{
    if (s_rmt_initialized)
    {
        return ESP_OK;
    }

    esp_err_t ret;

    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << MATRIX_DATA_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", MATRIX_DATA_GPIO, esp_err_to_name(ret));
        return ret;
    }

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = RMT_QUEUE_DEPTH,
        .gpio_num = MATRIX_DATA_GPIO,
        .flags = {
            .with_dma = true,
        },
    };

    ret = rmt_new_tx_channel(&tx_chan_config, &s_rmt_channel);
    if (ret != ESP_OK)
    {
        tx_chan_config.flags.with_dma = false;
        ret = rmt_new_tx_channel(&tx_chan_config, &s_rmt_channel);
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
        return ret;
    }
    s_rmt_with_dma = tx_chan_config.flags.with_dma;

    rmt_bytes_encoder_config_t bytes_encoder_cfg = {
        .bit0 = {
            .level0 = 1,
            .duration0 = WS2812B_T0H,
            .level1 = 0,
            .duration1 = WS2812B_T0L,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = WS2812B_T1H,
            .level1 = 0,
            .duration1 = WS2812B_T1L,
        },
        .flags.msb_first = true,
    };

    ret = rmt_new_bytes_encoder(&bytes_encoder_cfg, &s_rmt_encoder);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create RMT bytes encoder: %s", esp_err_to_name(ret));
        rmt_del_channel(s_rmt_channel);
        s_rmt_channel = NULL;
        return ret;
    }

    ret = rmt_enable(s_rmt_channel);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_rmt_encoder);
        rmt_del_channel(s_rmt_channel);
        s_rmt_encoder = NULL;
        s_rmt_channel = NULL;
        return ret;
    }

    s_rmt_initialized = true;
    ESP_LOGI(TAG,
             "RMT ready gpio=%d dma=%s color_order=%s layout=%s origin=%s odd_rows_reversed=%d",
             MATRIX_DATA_GPIO,
             s_rmt_with_dma ? "on" : "off",
             matrix_color_order_to_str(),
             matrix_layout_to_str(),
             matrix_origin_to_str(),
             (int)MATRIX_SERPENTINE_ODD_ROWS_REVERSED);
    return ESP_OK;
}

static esp_err_t rmt_matrix_transmit(const uint8_t *data, size_t size)
{
    if (!s_rmt_initialized || !s_rmt_channel || !s_rmt_encoder || !data)
    {
        ESP_LOGW(TAG, "RMT not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (size != MATRIX_RGB_FRAME_SIZE)
    {
        return ESP_ERR_INVALID_ARG;
    }

    matrix_prepare_tx_buffer(data, s_tx_buffer, size);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    esp_err_t ret = rmt_transmit(s_rmt_channel, s_rmt_encoder, s_tx_buffer, size, &tx_config);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to start RMT TX: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_tx_wait_all_done(s_rmt_channel, RMT_TIMEOUT_MS);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "RMT TX wait timeout/error: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void matrix_consumer_task(void *arg)
{
    (void)arg;
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

        if (s_rmt_initialized)
        {
            esp_err_t ret = rmt_matrix_transmit(s_front_buffer, MATRIX_RGB_FRAME_SIZE);
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "RMT transmit failed: %s", esp_err_to_name(ret));
            }
        }

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

    ESP_LOGI(TAG, "Initializing matrix task...");

    s_matrix_queue = xQueueCreate(MATRIX_QUEUE_LEN, MATRIX_RGB_FRAME_SIZE);
    if (s_matrix_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create matrix queue");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = rmt_matrix_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "RMT initialization failed: %s", esp_err_to_name(ret));
        vQueueDelete(s_matrix_queue);
        s_matrix_queue = NULL;
        return ret;
    }

    memset(s_front_buffer_storage, 0, sizeof(s_front_buffer_storage));
    memset(s_back_buffer_storage, 0, sizeof(s_back_buffer_storage));

    BaseType_t created = xTaskCreate(matrix_consumer_task, "matrix_consumer_task", 6144, NULL, 6, NULL);
    if (created != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create matrix task");
        vQueueDelete(s_matrix_queue);
        s_matrix_queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Matrix task started (queue=%d, frame=%d, gpio=%d)", MATRIX_QUEUE_LEN, MATRIX_RGB_FRAME_SIZE, MATRIX_DATA_GPIO);
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

esp_err_t deinit_matrix_task(void)
{
    if (s_rmt_encoder != NULL)
    {
        rmt_del_encoder(s_rmt_encoder);
        s_rmt_encoder = NULL;
    }

    if (s_rmt_channel != NULL)
    {
        esp_err_t ret = rmt_disable(s_rmt_channel);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to disable RMT: %s", esp_err_to_name(ret));
        }

        ret = rmt_del_channel(s_rmt_channel);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to delete RMT channel: %s", esp_err_to_name(ret));
        }

        s_rmt_channel = NULL;
        s_rmt_initialized = false;
        s_rmt_with_dma = false;
        ESP_LOGI(TAG, "RMT channel released");
    }

    if (s_matrix_queue != NULL)
    {
        vQueueDelete(s_matrix_queue);
        s_matrix_queue = NULL;
    }

    return ESP_OK;
}
