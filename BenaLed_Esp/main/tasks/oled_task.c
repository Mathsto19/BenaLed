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
#define FONT_SCALE_X 3
#define FONT_SCALE_Y 4
#define FONT_SPACING 1

#define ANIM_LETTER_STEP_FRAMES 7
#define ANIM_POST_REVEAL_FRAMES 140
#define ANIM_CURSOR_ON_FRAMES 5

typedef struct
{
    char ch;
    uint8_t rows[FONT_HEIGHT];
} glyph5x7_t;

typedef struct
{
    uint8_t x;
    uint8_t y;
    uint8_t speed;
    uint8_t duty;
} subtle_star_t;

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

static const subtle_star_t s_subtle_stars[] = {
    {8, 5, 5, 1},
    {20, 10, 6, 1},
    {34, 6, 7, 2},
    {52, 11, 5, 1},
    {74, 5, 6, 1},
    {95, 9, 7, 2},
    {112, 6, 5, 1},
    {15, 56, 6, 1},
    {30, 52, 7, 2},
    {49, 57, 5, 1},
    {80, 53, 6, 1},
    {103, 56, 7, 2},
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

static int wrap_mod(int value, int mod)
{
    int out = value % mod;
    if (out < 0)
    {
        out += mod;
    }
    return out;
}

static int tri_wave(int t, int period, int amp)
{
    const int half = period / 2;
    const int phase = wrap_mod(t, period);
    const int rise = (phase < half) ? phase : (period - phase);
    return ((rise * 2 * amp) / half) - amp;
}

static void draw_subtle_stars(int x0, int y0, int text_width, int text_height, uint32_t frame)
{
    const int text_left = x0 - 2;
    const int text_right = x0 + text_width + 1;
    const int text_top = y0 - 1;
    const int text_bottom = y0 + text_height;

    for (size_t i = 0; i < (sizeof(s_subtle_stars) / sizeof(s_subtle_stars[0])); i++)
    {
        const subtle_star_t *star = &s_subtle_stars[i];

        if (star->x >= text_left && star->x <= text_right &&
            star->y >= text_top && star->y <= text_bottom)
        {
            continue;
        }

        const uint32_t phase = ((frame / star->speed) + (uint32_t)(i * 3U)) % 28U;
        if (phase < star->duty)
        {
            oled_set_pixel(star->x, star->y, true);

            if (phase == 0U)
            {
                if ((i & 1U) == 0U)
                {
                    oled_set_pixel((int)star->x - 1, star->y, true);
                }
                else
                {
                    oled_set_pixel((int)star->x + 1, star->y, true);
                }
            }
        }
    }
}

static void draw_side_beacons(int x0, int y0, int text_width, int text_height, uint32_t frame)
{
    const int left = x0 - 5;
    const int right = x0 + text_width + 4;
    const int top = y0 - 1;
    const int height = text_height + 2;
    const int sweep = top + wrap_mod((int)frame * 2, height + 10) - 5;

    for (int y = top; y < (top + height); y += 3)
    {
        if (((y + (int)(frame >> 1)) & 0x01) == 0)
        {
            oled_set_pixel(left, y, true);
            oled_set_pixel(right, y, true);
        }
    }

    for (int i = 0; i < 4; i++)
    {
        const int y = sweep + i;
        if (y >= top && y < (top + height))
        {
            oled_set_pixel(left, y, true);
            oled_set_pixel(right, y, true);

            if (i == 1 || i == 2)
            {
                oled_set_pixel(left + 1, y, true);
                oled_set_pixel(right - 1, y, true);
            }
        }
    }
}

static void draw_orbit_dots(int x0, int y0, int text_width, int text_height, uint32_t frame)
{
    const int cx = x0 + (text_width / 2);
    const int cy = y0 + (text_height / 2);
    const int rx = (text_width / 2) + 6;
    const int ry = (text_height / 2) + 4;

    const int p1x = cx + tri_wave((int)frame * 2, 96, rx);
    const int p1y = cy + tri_wave((int)frame * 3, 72, ry);
    const int p2x = cx + tri_wave(((int)frame * 2) + 48, 96, rx - 2);
    const int p2y = cy + tri_wave(((int)frame * 3) + 36, 72, ry - 2);

    oled_set_pixel(p1x, p1y, true);
    oled_set_pixel(p2x, p2y, true);
    if ((frame & 0x01) == 0)
    {
        oled_set_pixel(p1x + 1, p1y, true);
        oled_set_pixel(p2x - 1, p2y, true);
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

static void draw_glyph_scaled(int x, int y, char ch, int reveal_x, int scan_x, bool pulse_on, uint32_t frame)
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

            const int start_x = x + (col * FONT_SCALE_X);
            const int start_y = y + (row * FONT_SCALE_Y);

            for (int dy = 0; dy < FONT_SCALE_Y; dy++)
            {
                for (int dx = 0; dx < FONT_SCALE_X; dx++)
                {
                    const int px = start_x + dx;
                    const int py = start_y + dy;

                    if (px > reveal_x)
                    {
                        continue;
                    }

                    oled_set_pixel(px, py, true);

                    if (px >= scan_x && px < (scan_x + 2) &&
                        ((px + py + (int)frame) & 0x01) == 0)
                    {
                        oled_set_pixel(px, py - 1, true);
                        oled_set_pixel(px, py + 1, true);
                    }

                    if (pulse_on &&
                        (dx == 0 || dx == (FONT_SCALE_X - 1) || dy == 0 || dy == (FONT_SCALE_Y - 1)) &&
                        ((px + py + (int)frame) % 19) == 0)
                    {
                        oled_set_pixel(px + 1, py, true);
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
        width += FONT_WIDTH * FONT_SCALE_X;
        if (*(c + 1) != '\0')
        {
            width += FONT_SPACING;
        }
    }

    return width;
}

static void draw_typing_cursor(int x, int y, int h, uint32_t frame)
{
    if ((frame % ANIM_LETTER_STEP_FRAMES) >= ANIM_CURSOR_ON_FRAMES)
    {
        return;
    }

    for (int yy = y - 1; yy < (y + h + 1); yy++)
    {
        oled_set_pixel(x, yy, true);
        oled_set_pixel(x + 1, yy, true);
    }
}

static void render_logo_frame(uint32_t frame)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    const int text_width = text_width_px(OLED_TEXT);
    const int text_height = FONT_HEIGHT * FONT_SCALE_Y;
    const int text_len = (int)strlen(OLED_TEXT);
    const int x0 = (OLED_WIDTH - text_width) / 2;
    const int y0 = (OLED_HEIGHT - text_height) / 2;
    const int reveal_window = text_len * ANIM_LETTER_STEP_FRAMES;
    const int cycle_frames = reveal_window + ANIM_POST_REVEAL_FRAMES;
    const int cycle_frame = (int)(frame % (uint32_t)cycle_frames);
    int visible_letters = cycle_frame / ANIM_LETTER_STEP_FRAMES;
    const int letter_progress = cycle_frame % ANIM_LETTER_STEP_FRAMES;
    const bool typing_phase = visible_letters < text_len;
    int scan_x = x0 - 8;
    bool pulse_on = true;

    if (visible_letters > text_len)
    {
        visible_letters = text_len;
    }

    if (typing_phase)
    {
        scan_x = x0 + (visible_letters * ((FONT_WIDTH * FONT_SCALE_X) + FONT_SPACING));
        pulse_on = ((letter_progress & 0x01) == 0);
    }
    else
    {
        const int post_frame = cycle_frame - reveal_window;
        const int scan_period = 72;
        scan_x = (x0 - 8) + ((text_width + 16) * (post_frame % scan_period) / scan_period);
        pulse_on = (((post_frame / 10) & 0x01) == 0);
    }

    draw_subtle_stars(x0, y0, text_width, text_height, frame);
    draw_side_beacons(x0, y0, text_width, text_height, frame);
    if (!typing_phase)
    {
        draw_orbit_dots(x0, y0, text_width, text_height, frame);
    }

    int cursor_x = x0;
    for (int i = 0; i < text_len; i++)
    {
        int reveal_x = cursor_x - 1;

        if (i < visible_letters)
        {
            reveal_x = cursor_x + (FONT_WIDTH * FONT_SCALE_X);
        }
        else if (i == visible_letters && typing_phase)
        {
            int revealed_cols = ((letter_progress + 1) * FONT_WIDTH) / ANIM_LETTER_STEP_FRAMES;
            if (revealed_cols > FONT_WIDTH)
            {
                revealed_cols = FONT_WIDTH;
            }
            reveal_x = cursor_x + (revealed_cols * FONT_SCALE_X) - 1;
        }

        draw_glyph_scaled(cursor_x, y0, OLED_TEXT[i], reveal_x, scan_x, pulse_on, frame);
        cursor_x += (FONT_WIDTH * FONT_SCALE_X) + FONT_SPACING;
    }

    if (typing_phase)
    {
        const int cursor_letter_x = x0 + (visible_letters * ((FONT_WIDTH * FONT_SCALE_X) + FONT_SPACING));
        int revealed_cols = ((letter_progress + 1) * FONT_WIDTH) / ANIM_LETTER_STEP_FRAMES;
        if (revealed_cols > FONT_WIDTH)
        {
            revealed_cols = FONT_WIDTH;
        }
        const int typing_cursor_x = cursor_letter_x + (revealed_cols * FONT_SCALE_X);

        if (typing_cursor_x < (x0 + text_width + 3))
        {
            draw_typing_cursor(typing_cursor_x, y0, text_height, frame);
        }
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
