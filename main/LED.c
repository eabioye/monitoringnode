#include <stdio.h>
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "LED.h"  // Include the header for the function declarations
#include "esp_log.h"


#define RED_PIN    21
#define GREEN_PIN  19
#define BLUE_PIN   17

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_RED         LEDC_CHANNEL_0
#define LEDC_OUTPUT_GREEN       LEDC_CHANNEL_1
#define LEDC_OUTPUT_BLUE        LEDC_CHANNEL_2
#define LEDC_FREQUENCY          5000
#define LEDC_RESOLUTION         LEDC_TIMER_13_BIT

void configure_ledc() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_APB_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel[3] = {
        { .channel = LEDC_OUTPUT_RED, .duty = 0, .gpio_num = RED_PIN, .speed_mode = LEDC_MODE, .hpoint = 0, .timer_sel = LEDC_TIMER },
        { .channel = LEDC_OUTPUT_GREEN, .duty = 0, .gpio_num = GREEN_PIN, .speed_mode = LEDC_MODE, .hpoint = 0, .timer_sel = LEDC_TIMER },
        { .channel = LEDC_OUTPUT_BLUE, .duty = 0, .gpio_num = BLUE_PIN, .speed_mode = LEDC_MODE, .hpoint = 0, .timer_sel = LEDC_TIMER }
    };

    for (int i = 0; i < 3; i++) {
        ledc_channel_config(&ledc_channel[i]);
    }
}
static const char *LED_TAG = "LED";
void setColor(int red, int green, int blue) {
    //ESP_LOGI(LED_TAG, "Setting LED color to R:%d G:%d B:%d", red, green, blue);

    ledc_set_duty(LEDC_MODE, LEDC_OUTPUT_RED, red);
    ledc_update_duty(LEDC_MODE, LEDC_OUTPUT_RED);
    
    ledc_set_duty(LEDC_MODE, LEDC_OUTPUT_GREEN, green);
    ledc_update_duty(LEDC_MODE, LEDC_OUTPUT_GREEN);
    
    ledc_set_duty(LEDC_MODE, LEDC_OUTPUT_BLUE, blue);
    ledc_update_duty(LEDC_MODE, LEDC_OUTPUT_BLUE);
}
