#ifndef MATRIX_TASK_H
#define MATRIX_TASK_H

#include "app_config.h"

esp_err_t init_matrix_task(void);
esp_err_t matrix_queue_push(const uint8_t *rgb_frame, size_t len);
const uint8_t *matrix_get_front_buffer(void);
size_t matrix_get_frame_size(void);
uint32_t matrix_get_frame_counter(void);

#endif // MATRIX_TASK_H