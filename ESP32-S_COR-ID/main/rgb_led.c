


#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

#define RED_GPIO    25
#define GREEN_GPIO  26
#define BLUE_GPIO   27

#define LEDC_FREQ_HZ       5000
#define LEDC_RESOLUTION    LEDC_TIMER_8_BIT

#define RED_CHANNEL   LEDC_CHANNEL_0
#define GREEN_CHANNEL LEDC_CHANNEL_1
#define BLUE_CHANNEL  LEDC_CHANNEL_2

#define LEDC_TIMER     LEDC_TIMER_0
#define LEDC_MODE      LEDC_HIGH_SPEED_MODE

static const char *TAG = "RGB_PWM";

void init_rgb_pwm() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_RESOLUTION,
        .freq_hz          = LEDC_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t channels[] = {
        {.channel = RED_CHANNEL,   .duty = 0, .gpio_num = RED_GPIO,   .speed_mode = LEDC_MODE, .hpoint = 0, .timer_sel = LEDC_TIMER},
        {.channel = GREEN_CHANNEL, .duty = 0, .gpio_num = GREEN_GPIO, .speed_mode = LEDC_MODE, .hpoint = 0, .timer_sel = LEDC_TIMER},
        {.channel = BLUE_CHANNEL,  .duty = 0, .gpio_num = BLUE_GPIO,  .speed_mode = LEDC_MODE, .hpoint = 0, .timer_sel = LEDC_TIMER}
    };

    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(ledc_channel_config(&channels[i]));
    }

    ESP_LOGI(TAG, "RGB PWM initialized on GPIO %d (R), %d (G), %d (B)", RED_GPIO, GREEN_GPIO, BLUE_GPIO);
}

void set_rgb_color(uint8_t r, uint8_t g, uint8_t b) {
    // 8-bit color to PWM duty (0–255)
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, RED_CHANNEL, r));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, RED_CHANNEL));

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, GREEN_CHANNEL, g));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, GREEN_CHANNEL));

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, BLUE_CHANNEL, b));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, BLUE_CHANNEL));
}