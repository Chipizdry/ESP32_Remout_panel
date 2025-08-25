


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "string.h"
#include "esp_task_wdt.h"
#include "esp_hid_common.h"
#include "esp_hidd.h" 

#define TAG "TELEMETRY"
#define DEVICE_NAME "COR-VELO"
#define SERVICE_UUID 0xFFF0
#define CHAR_UUID_TX 0xFFF1
#define CHAR_UUID_RX 0xFFF2
#define GATTS_NUM_HANDLE 6
#define TELEMETRY_APP_ID 0
#define MAX_MTU_SIZE 517
#define QUEUE_SIZE 50


#define ESP_HIDD_EVENT_BLE_CONNECT 0
#define ESP_HIDD_EVENT_BLE_DISCONNECT 1
#define ESP_HIDD_EVENT_BLE_ERROR 2

// HID Report Map для комбинированного устройства (Keyboard + Mouse + Gamepad)
static const uint8_t hid_report_map[] = {
    // Keyboard
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x05,        //   Usage Maximum (5)
    0x91, 0x02,        //   Output (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Const,Array,Abs)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data,Array,Abs)
    0xC0,              // End Collection

    // Mouse
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x03,        //     Usage Maximum (3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x01,        //     Input (Const,Array,Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection

    // Gamepad
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x10,        //   Usage Maximum (16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x33,        //   Usage (Rx)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0xC0               // End Collection
};

static esp_hidd_dev_t *hid_dev = NULL;  
static bool hid_connected = false;

// Структуры для HID отчетов
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} hid_keyboard_report_t;

typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
} hid_mouse_report_t;

typedef struct {
    uint16_t buttons;
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t rz;
} hid_gamepad_report_t;

static hid_keyboard_report_t keyboard_report = {0};
static hid_mouse_report_t mouse_report = {0};
static hid_gamepad_report_t gamepad_report = {0};

static uint8_t telemetry_char_value[512];
static uint8_t rx_data[512];
static bool notifications_enabled = false;
static bool device_connected = false;
static QueueHandle_t data_queue = NULL;


// Параметры рекламы для быстрого соединения
// === Глобальные объекты ===
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x100,
    .adv_int_max = 0x200,  
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
};

// UUID сервиса
uint8_t service_uuid[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, (SERVICE_UUID & 0xFF), (SERVICE_UUID >> 8), 0x00, 0x00
};

// Данные для рекламы

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    //.appearance = ESP_BLE_APPEARANCE_HID_KEYBOARD,
    .appearance =ESP_BLE_APPEARANCE_GENERIC_HID,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(service_uuid),
    .p_service_uuid = service_uuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};



static uint16_t telemetry_handle_table[GATTS_NUM_HANDLE];
static esp_gatt_if_t telemetry_gatts_if = 0;
static uint16_t telemetry_conn_id = 0;

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void telemetry_notify_task(void *arg);
static void data_generator_task(void *arg);


// GATT атрибуты
static esp_gatts_attr_db_t telemetry_gatt_db[GATTS_NUM_HANDLE] = {
    // Service Declaration
    [0] = {
        .attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t[]){0x00, 0x28},
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint16_t),
            .length = sizeof(uint16_t),
            .value = (uint8_t[]){(SERVICE_UUID & 0xFF), (SERVICE_UUID >> 8)}
        }
    },
    // Characteristic Declaration (TX)
    [1] = {
        .attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t[]){0x03, 0x28},
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint8_t),
            .length = sizeof(uint8_t),
            .value = (uint8_t[]){ESP_GATT_CHAR_PROP_BIT_NOTIFY}
        }
    },
    // Characteristic Value (TX)
    [2] = {
        .attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t[]){CHAR_UUID_TX & 0xFF, CHAR_UUID_TX >> 8},
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(telemetry_char_value),
            .length = 0, // Начальная длина 0
            .value = telemetry_char_value // Указатель на буфер
        }
    },
    // Client Characteristic Configuration Descriptor
    [3] = {
        .attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t[]){0x02, 0x29},
            .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            .max_length = sizeof(uint16_t),
            .length = sizeof(uint16_t),
            .value = (uint8_t[]){0x00, 0x00} // Начальное значение 0
        }
    },
    // Characteristic Declaration (RX)
    [4] = {
        .attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t[]){0x03, 0x28},
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint8_t),
            .length = sizeof(uint8_t),
            .value = (uint8_t[]){ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR}
        }
    },
    // Characteristic Value (RX)
    [5] = {
        .attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP},
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t[]){CHAR_UUID_RX & 0xFF, CHAR_UUID_RX >> 8},
            .perm = ESP_GATT_PERM_WRITE,
            .max_length = sizeof(rx_data),
            .length = 0, // Начальная длина 0
            .value = rx_data // Указатель на буфер
        }
    }
};


static void hid_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    switch (id) {
        case ESP_HIDD_EVENT_BLE_CONNECT:
            hid_connected = true;
            ESP_LOGI(TAG, "HID Connected");
            break;
        case ESP_HIDD_EVENT_BLE_DISCONNECT:
            hid_connected = false;
            ESP_LOGI(TAG, "HID Disconnected");
            break;
        case ESP_HIDD_EVENT_BLE_ERROR:
            ESP_LOGE(TAG, "HID Error");
            break;
        default:
            break;
    }
}

// Обработчик событий HID (ДОБАВЛЕНО)
/*
static void hid_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    switch (id) {
        case ESP_HIDD_EVENT_BLE_CONNECT:
            hid_connected = true;
            ESP_LOGI(TAG, "HID Device Connected");
            break;
            
        case ESP_HIDD_EVENT_BLE_DISCONNECT:
            hid_connected = false;
            ESP_LOGI(TAG, "HID Device Disconnected");
            break;
            
        case ESP_HIDD_EVENT_BLE_ERROR:
            ESP_LOGE(TAG, "HID Error occurred");
            break;
            
        default:
            break;
    }
}
*/

// Инициализация BLE
void telemetry_init(void) {
    data_queue = xQueueCreate(50, sizeof(uint8_t[MAX_MTU_SIZE]));
    memset(telemetry_char_value, 0, sizeof(telemetry_char_value));
    memset(rx_data, 0, sizeof(rx_data));

    // Настройка MTU
   // esp_ble_gatt_set_local_mtu(512);

    // Инициализация BLE
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.controller_task_stack_size = 8192;

    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
  //  ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(MAX_MTU_SIZE));

    // Инициализация HID устройства для ESP-IDF v5.3.1
    static esp_hid_raw_report_map_t raw_report_maps[] = {
        { .data = hid_report_map, .len = sizeof(hid_report_map) }
    };
    
    esp_hid_device_config_t hid_config = {
        .vendor_id        = 0x1234,
        .product_id       = 0x5678,
        .version          = 0x0100,
        .device_name      = "COR-VELO HID",
        .manufacturer_name= "COR-VELO",
        .serial_number    = "123456",
        .report_maps      = raw_report_maps,
        .report_maps_len  = 1,
    };
    // Инициализация HID устройства с правильными параметрами
    esp_err_t ret = esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE, hid_event_callback, &hid_dev);
    if (ret != ESP_OK || hid_dev == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HID device: %s", esp_err_to_name(ret));
    } else {
        
        ESP_LOGI(TAG, "HID device init OK, starting advertising");
        esp_ble_gap_start_advertising(&adv_params);
    }

    // Регистрируем обратные вызовы
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(TELEMETRY_APP_ID));
}







void send_mouse_report(int8_t x, int8_t y, uint8_t buttons) {
    if (hid_connected && hid_dev) {
        mouse_report.x = x;
        mouse_report.y = y;
        mouse_report.buttons = buttons;
        // Report ID 2 для мыши (как указано в report map)
        esp_hidd_dev_input_set(hid_dev, 0, 2, (uint8_t*)&mouse_report, sizeof(mouse_report));
    }
}

void send_gamepad_report(uint8_t x, uint8_t y, uint8_t z, uint8_t rz, uint16_t buttons) {
    if (hid_connected && hid_dev) {
        gamepad_report.x = x;
        gamepad_report.y = y;
        gamepad_report.z = z;
        gamepad_report.rz = rz;
        gamepad_report.buttons = buttons;
        // Report ID 3 для геймпада
        esp_hidd_dev_input_set(hid_dev, 0, 3, (uint8_t*)&gamepad_report, sizeof(gamepad_report));
    }
}

void send_keyboard_report(uint8_t keycode) {
    if (hid_connected && hid_dev) {
        keyboard_report.keycode[0] = keycode;
        // Report ID 1 для клавиатуры
        esp_hidd_dev_input_set(hid_dev, 0, 1, (uint8_t*)&keyboard_report, sizeof(keyboard_report));
        vTaskDelay(pdMS_TO_TICKS(40));
        keyboard_report.keycode[0] = 0;
        esp_hidd_dev_input_set(hid_dev, 0, 1, (uint8_t*)&keyboard_report, sizeof(keyboard_report));
    }
}





// Запуск задачи отправки данных
void telemetry_start(void) {
    xTaskCreate(telemetry_notify_task, "telemetry_notify_task", 4096, NULL, 5, NULL);
    xTaskCreate(data_generator_task, "data_generator_task", 4096, NULL, 5, NULL);
}




// Генератор тестовых данных (замените на реальные данные с датчиков)
static void data_generator_task(void *arg) {
    uint8_t data_packet[8];
    uint32_t counter = 0;
    const TickType_t xFrequency = pdMS_TO_TICKS(20); // 50Hz
    
    while (1) {
        vTaskDelay(xFrequency);
        
        // Заполнение данных
        int16_t accelX = 1000 + (counter % 100);
        int16_t accelY = 1000 + (counter % 100);
        int16_t accelZ = 1000 + (counter % 100);
        uint16_t angle = counter % 360;
        
        memcpy(&data_packet[0], &accelX, 2);
        memcpy(&data_packet[2], &accelY, 2);
        memcpy(&data_packet[4], &accelZ, 2);
        memcpy(&data_packet[6], &angle, 2);
        
        counter++;
        
        // Отправка в очередь с проверкой подключения
        if (device_connected && xQueueSend(data_queue, data_packet, 0) != pdTRUE) {
            static uint32_t dropped = 0;
            if (++dropped % 100 == 0) {
                ESP_LOGW(TAG, "Packets dropped: %lu", dropped);
            }
        }
    }
}

// Обработчик событий GAP
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(TAG, "Connection parameters updated");
            break;
        default:
            break;
    }
}

// Обработчик событий GATTS

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
    esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            telemetry_gatts_if = gatts_if;
            esp_ble_gap_set_device_name(DEVICE_NAME);
            esp_ble_gap_config_adv_data(&adv_data);
            esp_ble_gatt_set_local_mtu(MAX_MTU_SIZE);
            esp_ble_gatts_create_attr_tab(telemetry_gatt_db, gatts_if, GATTS_NUM_HANDLE, TELEMETRY_APP_ID);
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            memcpy(telemetry_handle_table, param->add_attr_tab.handles, sizeof(telemetry_handle_table));
            esp_ble_gatts_start_service(telemetry_handle_table[0]);
            break;

        case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "HID Device Connected");
            hid_connected = true;
            device_connected = true;
            telemetry_conn_id = param->connect.conn_id;
            
            // Запуск HID сервиса при подключении
          //  if (hid_dev) {
          //      esp_hidd_dev_start(hid_dev);
          //  }
            break;
        
        case ESP_GATTS_DISCONNECT_EVT:

        ESP_LOGI(TAG, "HID Device Disconnected");
            device_connected = false;
            notifications_enabled = false;
            hid_connected = false;
            esp_ble_gap_start_advertising(&adv_params);
            // Остановка HID сервиса
          //  if (hid_dev) {
          //      esp_hidd_dev_stop(hid_dev);
          //  }
            
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == telemetry_handle_table[3]) {
                // Обработка подписки на уведомления
                notifications_enabled = (*(uint16_t *)param->write.value) == 0x0001;
                ESP_LOGI(TAG, "Notifications %s", notifications_enabled ? "enabled" : "disabled");
            } else if (param->write.handle == telemetry_handle_table[5]) {
                // Обработка входящей команды для HID
                if (param->write.len >= 2) {
                    uint8_t command = param->write.value[0];
                    uint8_t value = param->write.value[1];
                    
                    switch (command) {
                        case 'M': // Mouse
                            send_mouse_report(value, value, 0);
                            break;
                        case 'G': // Gamepad
                            send_gamepad_report(value, value, value, value, 1);
                            break;
                        case 'K': // Keyboard
                            send_keyboard_report(value);
                        break;    
                    }
                }
                ESP_LOGI(TAG, "RX Command: %.*s", param->write.len, param->write.value);
            }
            break;

        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU updated: %d", param->mtu.mtu);
            break;

        default:
            break;
    }
}



// Задача отправки данных

static void telemetry_notify_task(void *arg) {
    uint8_t data_to_send[MAX_MTU_SIZE];
    uint32_t counter = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10)); // 100Hz
        
        if (device_connected && notifications_enabled) {
            // Заполняем данные
            for (int i = 0; i < 8; i++) {
                data_to_send[i] = counter++;
            }
            
            // Оригинальная отправка через GATT
            esp_err_t ret = esp_ble_gatts_send_indicate(
                telemetry_gatts_if,
                telemetry_conn_id,
                telemetry_handle_table[2],
                8,
                data_to_send,
                false
            );
            
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(ret));
            }
            
            // ДОБАВЛЕНО: Отправка HID данных на основе телеметрии
            if (hid_connected) {
                // Преобразование данных акселерометра в движение мыши/джойстика
                int8_t mouse_x = (data_to_send[0] - 128) / 10;
                int8_t mouse_y = (data_to_send[1] - 128) / 10;
                
                send_mouse_report(mouse_x, mouse_y, 0);
                
                // Или отправка как gamepad
                send_gamepad_report(
                    data_to_send[0], 
                    data_to_send[1], 
                    data_to_send[2], 
                    data_to_send[3], 
                    1 << (counter % 16)
                );
                
                // Периодическая отправка клавиш (пример)
                if (counter % 100 == 0) {
                    send_keyboard_report(0x04 + (counter % 10)); // Клавиши a-j
                }
            }
        }
    }
}




// Функция для деинициализации 
void telemetry_deinit(void) {
    if (hid_dev) {
        esp_hidd_dev_deinit(hid_dev);
        hid_dev = NULL;
    }
    
    if (data_queue) {
        vQueueDelete(data_queue);
        data_queue = NULL;
    }
    
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
}