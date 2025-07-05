


#ifndef I2C_PERIPHERAL_H
#define I2C_PERIPHERAL_H

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"

#define I2C_MASTER_SCL_IO           22      // Номер GPIO для тактового сигнала I2C
#define I2C_MASTER_SDA_IO           21      // Номер GPIO для сигнала данных I2C
#define I2C_MASTER_NUM              I2C_NUM_0 // Номер порта I2C
#define I2C_MASTER_FREQ_HZ          100000  // Частота тактового сигнала I2C
#define I2C_MASTER_TX_BUF_DISABLE   0       // Отключить буфер для режима мастера
#define I2C_MASTER_RX_BUF_DISABLE   0       // Отключить буфер для режима мастера
#define I2C_MASTER_TIMEOUT_MS       1000    // Таймаут в миллисекундах

/**
 * @brief Инициализация драйвера I2C в режиме мастера
 * 
 * @return esp_err_t ESP_OK при успехе, код ошибки в противном случае
 */
esp_err_t i2c_master_init(void);

/**
 * @brief Сканирование шины I2C на наличие устройств
 * 
 * @param found_addresses Массив для хранения найденных адресов
 * @param max_devices Максимальное количество устройств для сохранения
 * @return int Количество найденных устройств
 */
int i2c_scan(uint8_t *found_addresses, int max_devices);

/**
 * @brief Чтение данных с устройства I2C
 * 
 * @param dev_addr Адрес устройства
 * @param reg_addr Адрес регистра
 * @param data Указатель для хранения прочитанных данных
 * @param len Длина данных для чтения
 * @return esp_err_t ESP_OK при успехе, код ошибки в противном случае
 */
esp_err_t i2c_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, size_t len);

/**
 * @brief Запись данных на устройство I2C
 * 
 * @param dev_addr Адрес устройства
 * @param reg_addr Адрес регистра
 * @param data Данные для записи
 * @param len Длина данных для записи
 * @return esp_err_t ESP_OK при успехе, код ошибки в противном случае
 */
esp_err_t i2c_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, size_t len);

#endif // I2C_PERIPHERAL_H