

#ifndef BT_HID_H
#define BT_HID_H
#include <stddef.h>  // Для size_t
#include <stdbool.h> // Для bool

#ifdef CONFIG_BT_ENABLED

#include "esp_hidh.h"
#include "esp_bt.h"
#include <stddef.h>  // Для size_t
#include <stdbool.h> //

#ifdef __cplusplus
extern "C" {
#endif

void bt_hid_init(void);
void bt_hid_start_discovery(void);
void bt_hid_send_data(uint8_t *data, size_t length);
bool bt_hid_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_BT_ENABLED

#endif // BT_HID_H