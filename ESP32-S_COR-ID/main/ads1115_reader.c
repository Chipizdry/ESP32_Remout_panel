


#include "ads1115_reader.h"
#include "i2c_peripheral.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define ADS1115_REG_CONVERT  0x00
#define ADS1115_REG_CONFIG   0x01

static const char *TAG = "ADS1115";
float ads1115_voltages[ADS1115_NUM_CHANNELS] = {0};

static void ads1115_read_channel(uint8_t channel, float *voltage_out) {
    uint16_t config;

    switch (channel) {
        case 0: config = 0xC183; break; // AIN0-GND
        case 1: config = 0xD183; break; // AIN1-GND
        case 2: config = 0xE183; break; // AIN2-GND
        case 3: config = 0xF183; break; // AIN3-GND
        default: return;
    }

    uint8_t config_data[2] = { config >> 8, config & 0xFF };
    i2c_write(ADS1115_ADDR, ADS1115_REG_CONFIG, config_data, 2);

    vTaskDelay(pdMS_TO_TICKS(100)); // ожидание завершения

    uint8_t result[2] = {0};
    if (i2c_read(ADS1115_ADDR, ADS1115_REG_CONVERT, result, 2) != ESP_OK) {
      //  ESP_LOGE(TAG, "Ошибка чтения канала %d", channel);
        return;
    }

    int16_t raw = (result[0] << 8) | result[1];
    *voltage_out = raw * 6.144f / 32768.0f; // ±4.096V диапазон
}

static void ads1115_task(void *pvParameters) {
    while (1) {
        for (uint8_t i = 0; i < ADS1115_NUM_CHANNELS; i++) {
            ads1115_read_channel(i, &ads1115_voltages[i]);
        }

        for (uint8_t i = 0; i < ADS1115_NUM_CHANNELS; i++) {
            ESP_LOGI(TAG, "CH%d = %.3f V", i, ads1115_voltages[i]);
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 секунда
    }
}

void ads1115_reader_start(void) {
    xTaskCreate(ads1115_task, "ADS1115 Reader Task", 4096, NULL, 5, NULL);
}