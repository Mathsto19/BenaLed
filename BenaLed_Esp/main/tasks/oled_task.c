#include "oled_task.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"

#define OLED_FRAMEBUFFER_SIZE ((OLED_WIDTH * OLED_HEIGHT) / 8)
#define OLED_TEXT "BenaLed"
#define OLED_FRAME_TIME_MS 33
#define OLED_TASK_STACK_SIZE 4096
#define OLED_TASK_PRIORITY 4

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_SCALE 2
#define FONT_SPACING 2

#define ANIM_REVEAL_FRAMES 42
#define ANIM_CYCLE_FRAMES 180

typedef struct
{
    char ch;
    uint8_t rows[FONT_HEIGHT];
} glyph5x7_t;

typedef struct
{
    int8_t x;
    int8_t y;
    uint8_t speed;
    uint8_t duty;
} twinkle_t;

static const char *TAG = "oled_task";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_oled_dev = NULL;
static bool s_task_started = false;
static uint8_t s_framebuffer[OLED_FRAMEBUFFER_SIZE];

static const glyph5x7_t s_font_table[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'a', {0x00, 0x0E, 0x01, 0x0F, 0x11, 0x13, 0x0D}},
    {'d', {0x01, 0x01, 0x0D, 0x13, 0x11, 0x13, 0x0D}},
    {'e', {0x00, 0x0E, 0x11, 0x1F, 0x10, 0x11, 0x0E}},
    {'n', {0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x11}},
};

static const twinkle_t s_twinkles[] = {
    {6, 8, 3, 2},
    {18, 20, 5, 2},
    {27, 11, 4, 3},
    {42, 6, 6, 2},
    {57, 18, 5, 2},
    {73, 10, 3, 1},
    {87, 22, 4, 2},
    {105, 8, 6, 2},
    {118, 19, 5, 2},
    {12, 49, 4, 2},
    {30, 56, 6, 2},
    {48, 46, 3, 1},
    {66, 54, 5, 2},
    {82, 43, 4, 2},
    {98, 50, 6, 2},
    {116, 58, 5, 1},
};

static void oled_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
    {
        return;
    }

    const size_t index = (size_t)x + ((size_t)(y >> 3) * (size_t)OLED_WIDTH);
    const uint8_t bit = (uint8_t)(1U << (y & 0x07));

    if (on)
    {
        s_framebuffer[index] |= bit;
    }
    else
    {
        s_framebuffer[index] &= (uint8_t)~bit;
    }
}

static void oled_draw_rect_dotted(int x, int y, int w, int h, uint32_t frame)
{
    for (int xx = x; xx < (x + w); xx++)
    {
        if (((xx + (int)frame) & 0x01) == 0)
        {
            oled_set_pixel(xx, y, true);
            oled_set_pixel(xx, y + h - 1, true);
        }
    }

    for (int yy = y; yy < (y + h); yy++)
    {
        if (((yy + (int)frame) & 0x01) == 0)
        {
            oled_set_pixel(x, yy, true);
            oled_set_pixel(x + w - 1, yy, true);
        }
    }
}

static const uint8_t *font_rows_for_char(char ch)
{
    for (size_t i = 0; i < (sizeof(s_font_table) / sizeof(s_font_table[0])); i++)
    {
        if (s_font_table[i].ch == ch)
        {
            return s_font_table[i].rows;
        }
    }

    return s_font_table[1].rows; // fallback: 'B'
}

static void draw_glyph_scaled(int x, int y, char ch, int reveal_x, int shine_x, uint32_t frame)
{
    const uint8_t *rows = font_rows_for_char(ch);

    for (int row = 0; row < FONT_HEIGHT; row++)
    {
        const uint8_t pattern = rows[row];

        for (int col = 0; col < FONT_WIDTH; col++)
        {
            const uint8_t bit = (uint8_t)(1U << (FONT_WIDTH - 1 - col));
            if ((pattern & bit) == 0)
            {
                continue;
            }

            const int start_x = x + (col * FONT_SCALE);
            const int start_y = y + (row * FONT_SCALE);

            for (int dy = 0; dy < FONT_SCALE; dy++)
            {
                for (int dx = 0; dx < FONT_SCALE; dx++)
                {
                    const int px = start_x + dx;
                    const int py = start_y + dy;

                    if (px > reveal_x)
                    {
                        continue;
                    }

                    oled_set_pixel(px, py, true);

                    if (px >= shine_x && px < (shine_x + 3) &&
                        ((px + py + (int)frame) & 0x01) == 0)
                    {
                        oled_set_pixel(px, py - 1, true);
                    }
                }
            }
        }
    }
}

static int text_width_px(const char *text)
{
    int width = 0;

    for (const char *c = text; *c != '\0'; c++)
    {
        width += FONT_WIDTH * FONT_SCALE;
        if (*(c + 1) != '\0')
        {
            width += FONT_SPACING;
        }
    }

    return width;
}

static void draw_twinkles(uint32_t frame)
{
    for (size_t i = 0; i < (sizeof(s_twinkles) / sizeof(s_twinkles[0])); i++)
    {
        const twinkle_t *star = &s_twinkles[i];
        const uint32_t phase = (frame / star->speed) + (uint32_t)(i * 3U);
        const uint8_t blink = (uint8_t)(phase % 24U);

        if (blink < star->duty)
        {
            oled_set_pixel(star->x, star->y, true);
            if (blink == 0)
            {
                oled_set_pixel(star->x - 1, star->y, true);
                oled_set_pixel(star->x + 1, star->y, true);
                oled_set_pixel(star->x, star->y - 1, true);
                oled_set_pixel(star->x, star->y + 1, true);
            }
        }
    }
}

static void draw_reveal_cursor(int x, int y, int h, uint32_t frame)
{
    for (int yy = y - 2; yy < (y + h + 2); yy++)
    {
        if (((yy + (int)frame) & 0x01) == 0)
        {
            oled_set_pixel(x, yy, true);
        }
    }

    oled_set_pixel(x - 1, y - 3, true);
    oled_set_pixel(x, y - 3, true);
    oled_set_pixel(x + 1, y - 3, true);
    oled_set_pixel(x - 1, y + h + 2, true);
    oled_set_pixel(x, y + h + 2, true);
    oled_set_pixel(x + 1, y + h + 2, true);
}

static void draw_underline(int x, int width, int y, uint32_t frame)
{
    const int comet = (x - 6) + (int)((frame % 64U) * (uint32_t)(width + 12) / 64U);

    for (int xx = x - 2; xx < (x + width + 2); xx++)
    {
        if (((xx + (int)frame) & 0x03) == 0)
        {
            oled_set_pixel(xx, y, true);
        }

        int dist = xx - comet;
        if (dist < 0)
        {
            dist = -dist;
        }

        if (dist <= 2)
        {
            oled_set_pixel(xx, y - 1, true);
            if (dist == 0)
            {
                oled_set_pixel(xx, y - 2, true);
            }
        }
    }
}

static void render_logo_frame(uint32_t frame)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    draw_twinkles(frame);

    const int text_width = text_width_px(OLED_TEXT);
    const int text_height = FONT_HEIGHT * FONT_SCALE;
    const int x0 = (OLED_WIDTH - text_width) / 2;
    const int y0 = ((OLED_HEIGHT - text_height) / 2) - 2;

    const int cycle_frame = (int)(frame % ANIM_CYCLE_FRAMES);
    int reveal_x = x0 + text_width + 4;

    if (cycle_frame < ANIM_REVEAL_FRAMES)
    {
        reveal_x = (x0 - 6) + ((text_width + 12) * cycle_frame / ANIM_REVEAL_FRAMES);
        draw_reveal_cursor(reveal_x + 1, y0, text_height, frame);
    }

    const int shine_period = 76;
    const int shine_progress = (int)(frame % (uint32_t)shine_period);
    const int shine_x = (x0 - 14) + ((text_width + 28) * shine_progress / shine_period);

    int cursor_x = x0;
    for (const char *c = OLED_TEXT; *c != '\0'; c++)
    {
        draw_glyph_scaled(cursor_x, y0, *c, reveal_x, shine_x, frame);
        cursor_x += (FONT_WIDTH * FONT_SCALE) + FONT_SPACING;
    }

    if (((cycle_frame / 10) % 2) == 0)
    {
        oled_draw_rect_dotted(x0 - 4, y0 - 4, text_width + 8, text_height + 8, frame);
    }

    if (cycle_frame >= ANIM_REVEAL_FRAMES)
    {
        draw_underline(x0, text_width, y0 + text_height + 5, frame);
    }
}

static esp_err_t oled_send_command(uint8_t cmd)
{
    const uint8_t payload[2] = {0x00, cmd};
    return i2c_master_transmit(s_oled_dev, payload, sizeof(payload), -1);
}

static esp_err_t oled_flush(void)
{
    uint8_t payload[1 + OLED_WIDTH];
    payload[0] = 0x40;

    for (int page = 0; page < (OLED_HEIGHT / 8); page++)
    {
        esp_err_t ret = oled_send_command((uint8_t)(0xB0 + page));
        if (ret != ESP_OK)
        {
            return ret;
        }

        ret = oled_send_command(0x00);
        if (ret != ESP_OK)
        {
            return ret;
        }

        ret = oled_send_command(0x10);
        if (ret != ESP_OK)
        {
            return ret;
        }

        memcpy(&payload[1], &s_framebuffer[(size_t)page * (size_t)OLED_WIDTH], OLED_WIDTH);
        ret = i2c_master_transmit(s_oled_dev, payload, sizeof(payload), -1);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t oled_bus_init(void)
{
    if (s_i2c_bus != NULL && s_oled_dev != NULL)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = OLED_I2C_PORT_NUM,
        .sda_io_num = OLED_I2C_SDA_GPIO,
        .scl_io_num = OLED_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao criar bus I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDRESS,
        .scl_speed_hz = OLED_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_oled_dev);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao adicionar device OLED: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }

    return ESP_OK;
}

static esp_err_t oled_hardware_reset(void)
{
    if (OLED_RESET_GPIO < 0)
    {
        return ESP_OK;
    }

    const gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << OLED_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&rst_cfg);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpio_set_level(OLED_RESET_GPIO, 0);
    if (ret != ESP_OK)
    {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    ret = gpio_set_level(OLED_RESET_GPIO, 1);
    if (ret != ESP_OK)
    {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

static esp_err_t oled_power_enable(void)
{
    if (OLED_VEXT_CTRL_GPIO < 0)
    {
        return ESP_OK;
    }

    const gpio_config_t power_cfg = {
        .pin_bit_mask = 1ULL << OLED_VEXT_CTRL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&power_cfg);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpio_set_level(OLED_VEXT_CTRL_GPIO, OLED_VEXT_ACTIVE_LEVEL);
    if (ret != ESP_OK)
    {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

static esp_err_t oled_panel_init(void)
{
    const uint8_t init_sequence[] = {
        0xAE,       // display off
        0xD5, 0x80, // clock
        0xA8, 0x3F, // multiplex ratio for 64px
        0xD3, 0x00, // display offset
        0x40,       // start line
        0x8D, 0x14, // charge pump on
        0x20, 0x02, // page addressing mode
        0xA1,       // segment remap
        0xC8,       // COM scan direction remapped
        0xDA, 0x12, // COM pins
        0x81, 0xCF, // contrast
        0xD9, 0xF1, // pre-charge
        0xDB, 0x40, // VCOMH
        0xA4,       // display resume
        0xA6,       // normal display
        0x2E,       // stop scroll
        0xAF        // display on
    };

    esp_err_t ret = oled_power_enable();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao habilitar Vext da OLED: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = oled_hardware_reset();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha no reset da OLED: %s", esp_err_to_name(ret));
        return ret;
    }

    for (size_t i = 0; i < sizeof(init_sequence); i++)
    {
        ret = oled_send_command(init_sequence[i]);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Falha ao enviar init cmd 0x%02X: %s", init_sequence[i], esp_err_to_name(ret));
            return ret;
        }
    }

    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    return oled_flush();
}

static void oled_animation_task(void *arg)
{
    (void)arg;
    uint32_t frame = 0;

    while (1)
    {
        render_logo_frame(frame);
        esp_err_t ret = oled_flush();
        if (ret != ESP_OK && (frame % 30U) == 0U)
        {
            ESP_LOGW(TAG, "Falha ao atualizar frame OLED: %s", esp_err_to_name(ret));
        }

        frame++;
        vTaskDelay(pdMS_TO_TICKS(OLED_FRAME_TIME_MS));
    }
}

esp_err_t init_oled_task(void)
{
    if (s_task_started)
    {
        return ESP_OK;
    }

    esp_err_t ret = oled_bus_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = oled_panel_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    BaseType_t created = xTaskCreate(
        oled_animation_task,
        "oled_animation_task",
        OLED_TASK_STACK_SIZE,
        NULL,
        OLED_TASK_PRIORITY,
        NULL);

    if (created != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar task da OLED");
        return ESP_ERR_NO_MEM;
    }

    s_task_started = true;
    ESP_LOGI(TAG, "OLED iniciada com animacao BenaLed");
    return ESP_OK;
}
