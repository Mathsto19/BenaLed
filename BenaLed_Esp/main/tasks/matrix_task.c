#include "matrix_task.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "hal/gpio_hal.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "matrix_task";

// Configurações do RMT
#define RMT_GPIO_NUM 2
#define RMT_RESOLUTION_HZ (10 * 1000 * 1000) // 10 MHz
#define RMT_QUEUE_DEPTH 8
#define RMT_TIMEOUT_MS 1000

// WS2812B timing (10 MHz clock = 100ns/tick)
#define WS2812B_T0H 4 // ~400ns para bit 0 HIGH
#define WS2812B_T0L 8 // ~800ns para bit 0 LOW
#define WS2812B_T1H 7 // ~700ns para bit 1 HIGH
#define WS2812B_T1L 6 // ~600ns para bit 1 LOW

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

// RMT
static rmt_channel_handle_t s_rmt_channel = NULL;
static rmt_encoder_handle_t s_rmt_encoder = NULL;
static bool s_rmt_initialized = false;

// Buffer para trabalho (conversão RGB -> GRB)
static uint8_t s_tx_buffer[MATRIX_RGB_FRAME_SIZE];

/**
 * @brief Converte RGB para GRB (WS2812B está em ordem GRB)
 */
static void rgb_to_grb(const uint8_t *rgb, uint8_t *grb, size_t size)
{
    // size deve ser múltiplo de 3 (R, G, B)
    for (size_t i = 0; i < size; i += 3)
    {
        // RGB: [R, G, B] -> GRB: [G, R, B]
        grb[i + 0] = rgb[i + 1]; // G
        grb[i + 1] = rgb[i + 0]; // R
        grb[i + 2] = rgb[i + 2]; // B
    }
}

/**
 * @brief Inicializa RMT com DMA para transmissão de dados da matriz
 */
static esp_err_t rmt_matrix_init(void)
{
    if (s_rmt_initialized)
    {
        return ESP_OK;
    }

    esp_err_t ret;

    // Configurar GPIO para saída
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << RMT_GPIO_NUM);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao configurar GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "GPIO %d configurado para RMT", RMT_GPIO_NUM);

    // Configurar canal TX do RMT com DMA
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = RMT_QUEUE_DEPTH,
        .gpio_num = RMT_GPIO_NUM,
        .flags = {
            .with_dma = true, // Habilitar DMA
        },
    };

    ret = rmt_new_tx_channel(&tx_chan_config, &s_rmt_channel);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao criar canal RMT: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Canal RMT TX criado (GPIO: %d, Resolução: %d Hz, DMA: ativado)",
             RMT_GPIO_NUM, RMT_RESOLUTION_HZ);

    // Criar encoder de bytes padrão (transforma bytes em símbolos RMT)
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
        .flags.msb_first = true, // MSB primeiro (padrão WS2812B)
    };

    ret = rmt_new_bytes_encoder(&bytes_encoder_cfg, &s_rmt_encoder);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao criar bytes encoder: %s", esp_err_to_name(ret));
        rmt_del_channel(s_rmt_channel);
        return ret;
    }
    ESP_LOGI(TAG, "Bytes encoder criado (WS2812B timing)");

    // Habilitar canal RMT
    ret = rmt_enable(s_rmt_channel);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao ativar canal RMT: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_rmt_encoder);
        rmt_del_channel(s_rmt_channel);
        return ret;
    }
    ESP_LOGI(TAG, "Canal RMT TX ativado com sucesso");

    s_rmt_initialized = true;
    return ESP_OK;
}

/**
 * @brief Transmite buffer via RMT com DMA (convertendo RGB -> GRB)
 */
static esp_err_t rmt_matrix_transmit(const uint8_t *data, size_t size)
{
    if (!s_rmt_initialized || !s_rmt_channel || !s_rmt_encoder || !data)
    {
        ESP_LOGW(TAG, "RMT não inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    // Converter RGB -> GRB (WS2812B usa GRB)
    rgb_to_grb(data, s_tx_buffer, size);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    // Transmitir buffer convertido
    esp_err_t ret = rmt_transmit(s_rmt_channel, s_rmt_encoder, s_tx_buffer, size, &tx_config);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Erro ao iniciar transmissão RMT: %s", esp_err_to_name(ret));
        return ret;
    }

    // Aguardar conclusão da transmissão (com timeout)
    ret = rmt_tx_wait_all_done(s_rmt_channel, pdMS_TO_TICKS(RMT_TIMEOUT_MS));
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Timeout ao aguardar RMT: %s", esp_err_to_name(ret));
    }

    return ret;
}

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

        // Transmitir buffer via RMT com DMA
        if (s_rmt_initialized)
        {
            esp_err_t ret = rmt_matrix_transmit(s_front_buffer, MATRIX_RGB_FRAME_SIZE);
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "Falha na transmissão RMT: %s", esp_err_to_name(ret));
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

    ESP_LOGI(TAG, "Inicializando task de matriz...");

    s_matrix_queue = xQueueCreate(MATRIX_QUEUE_LEN, MATRIX_RGB_FRAME_SIZE);
    if (s_matrix_queue == NULL)
    {
        ESP_LOGE(TAG, "Falha ao criar fila de matriz");
        return ESP_ERR_NO_MEM;
    }

    // Inicializar RMT com DMA
    esp_err_t ret = rmt_matrix_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao inicializar RMT com DMA: %s", esp_err_to_name(ret));
        vQueueDelete(s_matrix_queue);
        s_matrix_queue = NULL;
        return ret;
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

    ESP_LOGI(TAG, "✓ Task de matriz iniciada (queue=%d, frame=%d bytes, RMT: ativo em GPIO %d)",
             MATRIX_QUEUE_LEN, MATRIX_RGB_FRAME_SIZE, RMT_GPIO_NUM);
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

/**
 * @brief Finaliza RMT com DMA (limpeza de recursos)
 */
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
            ESP_LOGE(TAG, "Erro ao desabilitar RMT: %s", esp_err_to_name(ret));
        }

        ret = rmt_del_channel(s_rmt_channel);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Erro ao deletar canal RMT: %s", esp_err_to_name(ret));
        }

        s_rmt_channel = NULL;
        s_rmt_initialized = false;
        ESP_LOGI(TAG, "Canal RMT finalizado com sucesso");
    }

    if (s_matrix_queue != NULL)
    {
        vQueueDelete(s_matrix_queue);
        s_matrix_queue = NULL;
    }

    return ESP_OK;
}
