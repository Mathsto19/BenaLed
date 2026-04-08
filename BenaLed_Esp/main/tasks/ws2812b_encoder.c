#include "ws2812b_encoder.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ws2812b_encoder";

/**
 * @brief Codificador WS2812B para RMT (versão simplificada)
 *
 * Gera pulsos RMT no formato WS2812B/NeoPixel em GPIO
 * Timing (com clock RMT 10 MHz = 100ns/tick):
 * - Bit 0: ~350ns HIGH (4 ticks) + ~800ns LOW (8 ticks)
 * - Bit 1: ~700ns HIGH (7 ticks) + ~600ns LOW (6 ticks)
 */

#define WS2812B_T0H 4
#define WS2812B_T0L 8
#define WS2812B_T1H 7
#define WS2812B_T1L 6

typedef struct
{
    rmt_encoder_t base;
    uint32_t symbol_buffer[512];
} ws2812b_encoder_context_t;

static size_t ws2812b_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                             const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    (void)channel; // Não usado neste encoder

    ws2812b_encoder_context_t *ctx = __containerof(encoder, ws2812b_encoder_context_t, base);
    const uint8_t *data = (const uint8_t *)primary_data;
    rmt_symbol_word_t *symbols = (rmt_symbol_word_t *)ctx->symbol_buffer;
    uint32_t symbol_idx = 0;

    // Converter cada byte RGB em 8 símbolos WS2812B
    for (size_t byte_idx = 0; byte_idx < data_size && symbol_idx < 504; byte_idx++)
    {
        uint8_t byte = data[byte_idx];

        for (int bit = 7; bit >= 0 && symbol_idx < 504; bit--)
        {
            uint8_t bit_val = (byte >> bit) & 0x01;

            if (bit_val)
            {
                symbols[symbol_idx].level0 = 1;
                symbols[symbol_idx].duration0 = WS2812B_T1H;
                symbols[symbol_idx].level1 = 0;
                symbols[symbol_idx].duration1 = WS2812B_T1L;
            }
            else
            {
                symbols[symbol_idx].level0 = 1;
                symbols[symbol_idx].duration0 = WS2812B_T0H;
                symbols[symbol_idx].level1 = 0;
                symbols[symbol_idx].duration1 = WS2812B_T0L;
            }
            symbol_idx++;
        }
    }

    *ret_state = RMT_ENCODING_COMPLETE;
    return data_size;
}

static esp_err_t ws2812b_reset(rmt_encoder_t *encoder)
{
    ws2812b_encoder_context_t *ctx = __containerof(encoder, ws2812b_encoder_context_t, base);
    memset(ctx->symbol_buffer, 0, sizeof(ctx->symbol_buffer));
    return ESP_OK;
}

static esp_err_t ws2812b_del(rmt_encoder_t *encoder)
{
    ws2812b_encoder_context_t *ctx = __containerof(encoder, ws2812b_encoder_context_t, base);
    free(ctx);
    return ESP_OK;
}

esp_err_t rmt_new_ws2812b_encoder(rmt_encoder_handle_t *ret_encoder)
{
    ESP_LOGI(TAG, "Criando encoder WS2812B");

    ws2812b_encoder_context_t *ctx = calloc(1, sizeof(ws2812b_encoder_context_t));
    if (!ctx)
    {
        ESP_LOGE(TAG, "Falha ao alocar encoder");
        return ESP_ERR_NO_MEM;
    }

    ctx->base.encode = ws2812b_encode;
    ctx->base.reset = ws2812b_reset;
    ctx->base.del = ws2812b_del;

    *ret_encoder = &ctx->base;

    ESP_LOGI(TAG, "✓ Encoder WS2812B criado (T0H=%u, T0L=%u, T1H=%u, T1L=%u)",
             WS2812B_T0H, WS2812B_T0L, WS2812B_T1H, WS2812B_T1L);
    return ESP_OK;
}
