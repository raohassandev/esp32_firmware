#include "waveshare_display_port.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CH422G_SYSTEM_ADDRESS 0x24U
#define CH422G_OUTPUT_ADDRESS 0x38U
#define CH422G_OUTPUT_ENABLE 0x01U
#define CH422G_TOUCH_RESET_LOW 0x2CU
#define CH422G_TOUCH_RESET_HIGH 0x2EU
#define CH422G_BACKLIGHT_ON 0x1EU
#define RGB_DATA_WIDTH 16U
#define RGB_DMA_BURST_SIZE 64U
#define I2C_TIMEOUT_MS 1000

static const char *TAG = "waveshare_display";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_ch422g_system;
static i2c_master_dev_handle_t s_ch422g_output;
static esp_lcd_touch_io_gt911_config_t s_gt911_io_cfg;
static bool s_ready;

static esp_err_t add_i2c_device(i2c_master_bus_handle_t bus,
                                uint16_t address,
                                uint32_t speed_hz,
                                i2c_master_dev_handle_t *out)
{
    if (!bus || !out || speed_hz == 0U) return ESP_ERR_INVALID_ARG;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = speed_hz,
    };
    return i2c_master_bus_add_device(bus, &cfg, out);
}

static esp_err_t ch422g_write(i2c_master_dev_handle_t device, uint8_t value)
{
    if (!device) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(device, &value, 1U, I2C_TIMEOUT_MS);
}

static esp_err_t ch422g_output_enable(void)
{
    return ch422g_write(s_ch422g_system, CH422G_OUTPUT_ENABLE);
}

static esp_err_t configure_touch_address_gpio(int gpio_num)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_num)) return ESP_ERR_INVALID_ARG;
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << (unsigned)gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

/* Preserve the exact CH422G/GPIO4 sequence from the pinned Waveshare LVGL9
 * baseline. GPIO4 participates in the GT911 reset/address-selection sequence;
 * CH422G also changes state, so this port deliberately does not re-label GPIO4
 * as the touch controller's sole reset pin. */
static esp_err_t reset_touch_controller(const waveshare_display_profile_t *profile)
{
    esp_err_t err = ch422g_output_enable();
    if (err != ESP_OK) return err;
    err = configure_touch_address_gpio(profile->gt911_address_select_gpio);
    if (err != ESP_OK) return err;

    err = ch422g_write(s_ch422g_output, CH422G_TOUCH_RESET_LOW);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));

    err = gpio_set_level(profile->gt911_address_select_gpio, 0);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));

    err = ch422g_write(s_ch422g_output, CH422G_TOUCH_RESET_HIGH);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

static esp_err_t create_i2c_bus(const waveshare_display_profile_t *profile,
                                i2c_master_bus_handle_t *out)
{
    if (!profile || !out) return ESP_ERR_INVALID_ARG;
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = profile->i2c_scl_gpio,
        .sda_io_num = profile->i2c_sda_gpio,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_cfg, out);
}

static esp_err_t create_ch422g_devices(const waveshare_display_profile_t *profile)
{
    esp_err_t err = add_i2c_device(s_bus, CH422G_SYSTEM_ADDRESS,
                                   profile->i2c_frequency_hz, &s_ch422g_system);
    if (err != ESP_OK) return err;
    err = add_i2c_device(s_bus, CH422G_OUTPUT_ADDRESS,
                         profile->i2c_frequency_hz, &s_ch422g_output);
    if (err != ESP_OK) {
        (void)i2c_master_bus_rm_device(s_ch422g_system);
        s_ch422g_system = NULL;
        return err;
    }
    return ESP_OK;
}

static esp_err_t create_rgb_panel_attempt(const waveshare_display_port_config_t *config,
                                          uint16_t bounce_lines,
                                          esp_lcd_panel_handle_t *panel)
{
    if (!config || !panel) return ESP_ERR_INVALID_ARG;
    const waveshare_display_profile_t *p = config->profile;
    const uint8_t frame_buffers =
        esp_lv_adapter_get_required_frame_buffer_count(config->tear_mode, config->rotation);

    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = p->pixel_clock_hz,
            .h_res = p->width,
            .v_res = p->height,
            .hsync_pulse_width = p->hsync_pulse_width,
            .hsync_back_porch = p->hsync_back_porch,
            .hsync_front_porch = p->hsync_front_porch,
            .vsync_pulse_width = p->vsync_pulse_width,
            .vsync_back_porch = p->vsync_back_porch,
            .vsync_front_porch = p->vsync_front_porch,
            .flags = {
                .pclk_active_neg = p->pclk_active_negative ? 1U : 0U,
            },
        },
        .data_width = RGB_DATA_WIDTH,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = frame_buffers,
        .bounce_buffer_size_px = (size_t)p->width * bounce_lines,
        .dma_burst_size = RGB_DMA_BURST_SIZE,
        .hsync_gpio_num = p->hsync_gpio,
        .vsync_gpio_num = p->vsync_gpio,
        .de_gpio_num = p->de_gpio,
        .pclk_gpio_num = p->pclk_gpio,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            p->data_gpio[0], p->data_gpio[1], p->data_gpio[2], p->data_gpio[3],
            p->data_gpio[4], p->data_gpio[5], p->data_gpio[6], p->data_gpio[7],
            p->data_gpio[8], p->data_gpio[9], p->data_gpio[10], p->data_gpio[11],
            p->data_gpio[12], p->data_gpio[13], p->data_gpio[14], p->data_gpio[15],
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    const size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    const size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_LOGI(TAG,
             "RGB %ux%u pclk=%lu Hz frame_buffers=%u bounce_lines=%u internal_dma_free=%u largest=%u",
             (unsigned)p->width, (unsigned)p->height,
             (unsigned long)p->pixel_clock_hz, (unsigned)frame_buffers,
             (unsigned)bounce_lines, (unsigned)dma_free, (unsigned)dma_largest);

    *panel = NULL;
    esp_err_t err = esp_lcd_new_rgb_panel(&panel_cfg, panel);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_init(*panel);
    if (err != ESP_OK) {
        (void)esp_lcd_panel_del(*panel);
        *panel = NULL;
    }
    return err;
}

static esp_err_t create_rgb_panel(const waveshare_display_port_config_t *config,
                                  esp_lcd_panel_handle_t *panel)
{
    const uint16_t requested_lines = config->bounce_buffer_lines;
    esp_err_t err = create_rgb_panel_attempt(config, requested_lines, panel);
    if (err == ESP_ERR_NO_MEM && requested_lines > 0U && config->allow_no_bounce_fallback) {
        ESP_LOGW(TAG,
                 "RGB bounce allocation unavailable; retrying direct PSRAM framebuffer mode");
        err = create_rgb_panel_attempt(config, 0U, panel);
    }
    return err;
}

static esp_err_t create_touch(const waveshare_display_profile_t *profile,
                              esp_lcd_panel_io_handle_t *touch_io,
                              esp_lcd_touch_handle_t *touch)
{
    if (!touch_io || !touch) return ESP_ERR_INVALID_ARG;
    *touch_io = NULL;
    *touch = NULL;

    esp_err_t err = reset_touch_controller(profile);
    if (err != ESP_OK) return err;

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = profile->i2c_frequency_hz;
    err = esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, touch_io);
    if (err != ESP_OK) return err;

    s_gt911_io_cfg.dev_addr = io_cfg.dev_addr;
    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = profile->width,
        .y_max = profile->height,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &s_gt911_io_cfg,
    };

    err = esp_lcd_touch_new_i2c_gt911(*touch_io, &touch_cfg, touch);
    if (err != ESP_OK) {
        (void)esp_lcd_panel_io_del(*touch_io);
        *touch_io = NULL;
    }
    return err;
}

esp_err_t waveshare_display_port_init(const waveshare_display_port_config_t *config,
                                      waveshare_display_port_handles_t *out)
{
    if (!config || !out || !waveshare_display_profile_valid(config->profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) return ESP_ERR_INVALID_STATE;

    memset(out, 0, sizeof(*out));
    memset(&s_gt911_io_cfg, 0, sizeof(s_gt911_io_cfg));
    s_bus = config->i2c_bus;
    if (!s_bus) {
        esp_err_t err = create_i2c_bus(config->profile, &s_bus);
        if (err != ESP_OK) return err;
        out->owns_i2c_bus = true;
    }
    out->i2c_bus = s_bus;

    esp_err_t err = create_ch422g_devices(config->profile);
    if (err != ESP_OK) goto fail;

    err = create_rgb_panel(config, &out->panel);
    if (err != ESP_OK) goto fail;

    if (config->enable_touch) {
        err = create_touch(config->profile, &out->touch_io, &out->touch);
        if (err != ESP_OK) goto fail;
    }

    s_ready = true;
    return ESP_OK;

fail:
    (void)waveshare_display_port_deinit(out);
    return err;
}

esp_err_t waveshare_display_port_backlight_on(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ch422g_output_enable();
    if (err != ESP_OK) return err;
    return ch422g_write(s_ch422g_output, CH422G_BACKLIGHT_ON);
}

esp_err_t waveshare_display_port_deinit(waveshare_display_port_handles_t *handles)
{
    if (!handles) return ESP_ERR_INVALID_ARG;
    esp_err_t first_error = ESP_OK;

    if (handles->touch) {
        esp_err_t err = esp_lcd_touch_del(handles->touch);
        if (first_error == ESP_OK && err != ESP_OK) first_error = err;
        handles->touch = NULL;
    }
    if (handles->touch_io) {
        esp_err_t err = esp_lcd_panel_io_del(handles->touch_io);
        if (first_error == ESP_OK && err != ESP_OK) first_error = err;
        handles->touch_io = NULL;
    }
    if (handles->panel) {
        esp_err_t err = esp_lcd_panel_del(handles->panel);
        if (first_error == ESP_OK && err != ESP_OK) first_error = err;
        handles->panel = NULL;
    }
    if (s_ch422g_output) {
        esp_err_t err = i2c_master_bus_rm_device(s_ch422g_output);
        if (first_error == ESP_OK && err != ESP_OK) first_error = err;
        s_ch422g_output = NULL;
    }
    if (s_ch422g_system) {
        esp_err_t err = i2c_master_bus_rm_device(s_ch422g_system);
        if (first_error == ESP_OK && err != ESP_OK) first_error = err;
        s_ch422g_system = NULL;
    }
    if (handles->owns_i2c_bus && handles->i2c_bus) {
        esp_err_t err = i2c_del_master_bus(handles->i2c_bus);
        if (first_error == ESP_OK && err != ESP_OK) first_error = err;
    }

    handles->i2c_bus = NULL;
    handles->owns_i2c_bus = false;
    s_bus = NULL;
    s_ready = false;
    memset(&s_gt911_io_cfg, 0, sizeof(s_gt911_io_cfg));
    return first_error;
}
