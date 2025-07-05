



#ifndef ADS1115_READER_H
#define ADS1115_READER_H

#include "esp_err.h"

#define ADS1115_NUM_CHANNELS 4
#define ADS1115_ADDR         0x4A  // Адрес по умолчанию

#ifdef __cplusplus
extern "C" {
#endif

extern float ads1115_voltages[ADS1115_NUM_CHANNELS];

void ads1115_reader_start(void);

#ifdef __cplusplus
}
#endif

#endif // ADS1115_READER_H