#ifndef WS2812B_ENCODER_H
#define WS2812B_ENCODER_H

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Cria um novo encoder WS2812B customizado para RMT
     *
     * O encoder converte bytes RGB em símbolos RMT no formato correto para
     * protocolo WS2812B (NeoPixel):
     * - Bit 0: ~350ns HIGH + ~800ns LOW
     * - Bit 1: ~700ns HIGH + ~600ns LOW
     *
     * @param ret_encoder Ponteiro para armazenar handle do encoder
     * @return ESP_OK se bem-sucedido, ESP_ERR_NO_MEM se falha em alocação
     */
    esp_err_t rmt_new_ws2812b_encoder(rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif

#endif // WS2812B_ENCODER_H
