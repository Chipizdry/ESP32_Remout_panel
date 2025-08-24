

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
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
#include <inttypes.h>

#define TAG "TELEMETRY"
#define DEVICE_NAME "COR-VELO"

#define TELEMETRY_SERVICE_UUID   0xFFF0
#define CHAR_UUID_TX   0xFFF1  // notify
#define CHAR_UUID_RX   0xFFF2  // write

#define GATTS_NUM_HANDLE  6
#define TELEMETRY_APP_ID  0
#define MAX_MTU_SIZE      517
#define QUEUE_SIZE        50

#define HID_SERVICE_UUID 0x1812

#define ESP_HIDD_EVENT_BLE_CONNECT 0
#define ESP_HIDD_EVENT_BLE_DISCONNECT 1
#define ESP_HIDD_EVENT_BLE_ERROR 2

// ---- Keyboard HID Report (Report ID 1) ----
static const uint8_t hid_report_keyboard[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       // Report ID 1
    0x05, 0x07,       // Usage Page (Keyboard)
    0x19, 0xE0,       // Usage Minimum (Left Control)
    0x29, 0xE7,       // Usage Maximum (Right GUI)
    0x15, 0x00,       // Logical Minimum (0)
    0x25, 0x01,       // Logical Maximum (1)
    0x75, 0x01,       // Report Size (1)
    0x95, 0x08,       // Report Count (8)
    0x81, 0x02,       // Input (Data,Var,Abs)
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,
    0x95, 0x05,
    0x75, 0x01,
    0x05, 0x08,
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,
    0xC0              // End Collection
};

// ---- Mouse HID Report (Report ID 2) ----
static const uint8_t hid_report_mouse[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x02,       // Report ID 2
    0x09, 0x01,       // Usage (Pointer)
    0xA1, 0x00,       // Collection (Physical)
    0x05, 0x09,       // Usage Page (Button)
    0x19, 0x01,       // Usage Minimum (Button 1)
    0x29, 0x03,       // Usage Maximum (Button 3)
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x03,
    0x75, 0x01,
    0x81, 0x02,       // Input (Data,Var,Abs)
    0x95, 0x01,
    0x75, 0x05,
    0x81, 0x01,       // Input (Cnst,Ary,Abs)
    0x05, 0x01,
    0x09, 0x30,       // Usage (X)
    0x09, 0x31,       // Usage (Y)
    0x15, 0x81,       // Logical Minimum (-127)
    0x25, 0x7F,       // Logical Maximum (127)
    0x75, 0x08,
    0x95, 0x02,
    0x81, 0x06,       // Input (Data,Var,Rel)
    0xC0,
    0xC0
};

// ---- Gamepad HID Report (Report ID 3) ----
static const uint8_t hid_report_gamepad[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Gamepad)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x03,       // Report ID 3
    0x05, 0x09,       // Usage Page (Button)
    0x19, 0x01,       // Usage Minimum (Button 1)
    0x29, 0x10,       // Usage Maximum (Button 16)
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x10,
    0x81, 0x02,       // Input (Data,Var,Abs)
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x30,       // Usage (X)
    0x09, 0x31,       // Usage (Y)
    0x09, 0x32,       // Usage (Z)
    0x09, 0x33,       // Usage (Rz)
    0x15, 0x00,
    0x26, 0xFF, 0x00, // Logical Maximum 255
    0x75, 0x08,
    0x95, 0x04,
    0x81, 0x02,       // Input (Data,Var,Abs)
    0xC0
};

typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} __attribute__((packed)) hid_keyboard_report_t;

typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
} __attribute__((packed)) hid_mouse_report_t;

typedef struct {
    uint16_t buttons;
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t rz;
} __attribute__((packed)) hid_gamepad_report_t;

static esp_hidd_dev_t *hid_dev = NULL;
static bool hid_connected = false;

static hid_keyboard_report_t keyboard_report = {0};
static hid_mouse_report_t    mouse_report    = {0};
static hid_gamepad_report_t  gamepad_report  = {0};

// ---- Telemetry GATT (Bluedroid) ----
static uint8_t telemetry_char_value[512];
static uint8_t rx_data[512];
static bool notifications_enabled = false;
static bool device_connected = false;
static QueueHandle_t data_queue = NULL;

static uint16_t telemetry_handle_table[GATTS_NUM_HANDLE];
static esp_gatt_if_t telemetry_gatts_if = ESP_GATT_IF_NONE;
static uint16_t telemetry_conn_id = 0xFFFF;

// ---- Обновлённые рекламные данные ----
// Включаем оба UUID: HID (0x1812) и кастомный (0xFFF0)
static uint16_t adv_service_uuid16[] = {
    HID_SERVICE_UUID,        // HID Service
    TELEMETRY_SERVICE_UUID   // Custom Telemetry Service
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp      = false,
    .include_name      = true,
    .include_txpower   = true,
    .min_interval      = 0x0006,
    .max_interval      = 0x0010,
    .appearance        = ESP_BLE_APPEARANCE_GENERIC_HID,
    .manufacturer_len  = 0,
    .p_manufacturer_data = NULL,
    .service_data_len  = 0,
    .p_service_data    = NULL,
    .service_uuid_len  = sizeof(adv_service_uuid16),
    .p_service_uuid    = (uint8_t *)adv_service_uuid16,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x100,
    .adv_int_max        = 0x200,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Attribute table for custom service 0xFFF0
static esp_gatts_attr_db_t telemetry_gatt_db[GATTS_NUM_HANDLE] = {
    // 0: Primary Service
    [0] = {
        .attr_control = { .auto_rsp = ESP_GATT_AUTO_RSP },
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p      = (uint8_t[]){0x00, 0x28}, // 0x2800
            .perm        = ESP_GATT_PERM_READ,
            .max_length  = sizeof(uint16_t),
            .length      = sizeof(uint16_t),
            .value       = (uint8_t[]){ (uint8_t)(TELEMETRY_SERVICE_UUID & 0xFF), (uint8_t)(TELEMETRY_SERVICE_UUID >> 8) }
        }
    },
    // 1: Char Declaration (TX notify)
    [1] = {
        .attr_control = { .auto_rsp = ESP_GATT_AUTO_RSP },
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p      = (uint8_t[]){0x03, 0x28}, // 0x2803
            .perm        = ESP_GATT_PERM_READ,
            .max_length  = sizeof(uint8_t),
            .length      = sizeof(uint8_t),
            .value       = (uint8_t[]){ ESP_GATT_CHAR_PROP_BIT_NOTIFY }
        }
    },
    // 2: Char Value (TX)
    [2] = {
        .attr_control = { .auto_rsp = ESP_GATT_AUTO_RSP },
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p      = (uint8_t[]){ (uint8_t)(CHAR_UUID_TX & 0xFF), (uint8_t)(CHAR_UUID_TX >> 8) },
            .perm        = ESP_GATT_PERM_READ,
            .max_length  = sizeof(telemetry_char_value),
            .length      = 0,
            .value       = telemetry_char_value
        }
    },
    // 3: CCCD
    [3] = {
        .attr_control = { .auto_rsp = ESP_GATT_AUTO_RSP },
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p      = (uint8_t[]){0x02, 0x29}, // 0x2902
            .perm        = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            .max_length  = sizeof(uint16_t),
            .length      = sizeof(uint16_t),
            .value       = (uint8_t[]){0x00, 0x00}
        }
    },
    // 4: Char Declaration (RX write/writeNR)
    [4] = {
        .attr_control = { .auto_rsp = ESP_GATT_AUTO_RSP },
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p      = (uint8_t[]){0x03, 0x28}, // 0x2803
            .perm        = ESP_GATT_PERM_READ,
            .max_length  = sizeof(uint8_t),
            .length      = sizeof(uint8_t),
            .value       = (uint8_t[]){ ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR }
        }
    },
    // 5: Char Value (RX)
    [5] = {
        .attr_control = { .auto_rsp = ESP_GATT_AUTO_RSP },
        .att_desc = {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p      = (uint8_t[]){ (uint8_t)(CHAR_UUID_RX & 0xFF), (uint8_t)(CHAR_UUID_RX >> 8) },
            .perm        = ESP_GATT_PERM_WRITE,
            .max_length  = sizeof(rx_data),
            .length      = 0,
            .value       = rx_data
        }
    },
};

// ---- Forward declarations ----
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void telemetry_notify_task(void *arg);
static void data_generator_task(void *arg);
static void hid_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);

// ---- HID helpers ----
static inline void send_mouse_report(int8_t x, int8_t y, uint8_t buttons) {
    if (!hid_connected || !hid_dev) return;
    mouse_report.x = x;
    mouse_report.y = y;
    mouse_report.buttons = buttons;
    // Report ID 2
    esp_hidd_dev_input_set(hid_dev, 0, 2, (uint8_t*)&mouse_report, sizeof(mouse_report));
}

static inline void send_gamepad_report(uint8_t x, uint8_t y, uint8_t z, uint8_t rz, uint16_t buttons) {
    if (!hid_connected || !hid_dev) return;
    gamepad_report.x = x;
    gamepad_report.y = y;
    gamepad_report.z = z;
    gamepad_report.rz = rz;
    gamepad_report.buttons = buttons;
    // Report ID 3
    esp_hidd_dev_input_set(hid_dev, 0, 3, (uint8_t*)&gamepad_report, sizeof(gamepad_report));
}

static inline void send_keyboard_report(uint8_t keycode) {
    if (!hid_connected || !hid_dev) return;
    memset(&keyboard_report, 0, sizeof(keyboard_report));
    keyboard_report.keycode[0] = keycode;        // press
    esp_hidd_dev_input_set(hid_dev, 0, 1, (uint8_t*)&keyboard_report, sizeof(keyboard_report));
    vTaskDelay(pdMS_TO_TICKS(20));
    keyboard_report.keycode[0] = 0;              // release
    esp_hidd_dev_input_set(hid_dev, 0, 1, (uint8_t*)&keyboard_report, sizeof(keyboard_report));
}

// ---- HID event cb ----
static void hid_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    ESP_LOGI(TAG, "HID event: base=%s, id=%ld", base, id);
    switch (id) {
        case ESP_HIDD_EVENT_BLE_CONNECT:
            hid_connected = true;
            ESP_LOGI(TAG, "HID connected");
            break;
        case ESP_HIDD_EVENT_BLE_DISCONNECT:
            hid_connected = false;
            ESP_LOGI(TAG, "HID disconnected");
            break;
        case ESP_HIDD_EVENT_BLE_ERROR:
            ESP_LOGE(TAG, "HID error");
            break;
        default:
            break;
    }
}

// ---- Обновлённый GAP handler ----
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    ESP_LOGI(TAG, "GAP event: %d", event);

    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            // Рекламные данные сконфигурированы — запускаем рекламу
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Advertising start failed, status=%d", param->adv_start_cmpl.status);
            } else {
                ESP_LOGI(TAG, "Advertising started with HID and Telemetry services");
            }
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising stopped");
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(TAG,
                     "Update conn params: status=%d, min_int=%d, max_int=%d, latency=%d, timeout=%d",
                     param->update_conn_params.status,
                     param->update_conn_params.min_int,
                     param->update_conn_params.max_int,
                     param->update_conn_params.latency,
                     param->update_conn_params.conn_int);
            break;

        default:
            ESP_LOGI(TAG, "Unhandled GAP event %d", event);
            break;
    }
}


// ---- GATTS handler ----
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {

    switch (event) {
        case ESP_GATTS_REG_EVT:
            telemetry_gatts_if = gatts_if;
            ESP_LOGI(TAG, "Telemetry service registered, gatts_if=%d", gatts_if);
            
            // Устанавливаем MTU после регистрации сервиса
            esp_ble_gatt_set_local_mtu(MAX_MTU_SIZE);
            
            // Создаём таблицу атрибутов для кастомного сервиса
            esp_ble_gatts_create_attr_tab(telemetry_gatt_db, gatts_if, GATTS_NUM_HANDLE, TELEMETRY_APP_ID);
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Create attr table failed, error 0x%X", param->add_attr_tab.status);
                break;
            }
            memcpy(telemetry_handle_table, param->add_attr_tab.handles, sizeof(telemetry_handle_table));
            esp_ble_gatts_start_service(telemetry_handle_table[0]);
            ESP_LOGI(TAG, "Telemetry service started");
            break;

        case ESP_GATTS_CONNECT_EVT:
            device_connected = true;
            telemetry_conn_id = param->connect.conn_id;
            ESP_LOGI(TAG, "GATT connected, conn_id=%d", telemetry_conn_id);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            device_connected = false;
            notifications_enabled = false;
            telemetry_conn_id = 0xFFFF;
            ESP_LOGI(TAG, "GATT disconnected, restart advertising");
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == telemetry_handle_table[3]) {
                // CCCD
                if (param->write.len == 2) {
                    uint16_t cccd = param->write.value[0] | (param->write.value[1] << 8);
                    notifications_enabled = (cccd & 0x0001);
                    ESP_LOGI(TAG, "Notifications %s", notifications_enabled ? "EN" : "DIS");
                }
            } else if (param->write.handle == telemetry_handle_table[5]) {
                // RX command: first byte = 'M'|'G'|'K', second byte = value
                if (param->write.len >= 2) {
                    uint8_t command = param->write.value[0];
                    uint8_t value   = param->write.value[1];
                    switch (command) {
                        case 'M': send_mouse_report((int8_t)value, (int8_t)value, 0); break;
                        case 'G': send_gamepad_report(value, value, value, value, 1); break;
                        case 'K': send_keyboard_report(value); break;
                        default:  break;
                    }
                }
                ESP_LOGI(TAG, "RX: %.*s", param->write.len, (char*)param->write.value);
            }
            break;

        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU updated: %d", param->mtu.mtu);
            break;
       
        default:
            break;
    }
}

// ---- Public init/start/stop ----
void telemetry_init(void) {
    esp_err_t ret;

    // --- Инициализация BT контроллера ---
    ESP_LOGI(TAG, "Initializing BT controller...");
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "BT Controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "BT Controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // --- Инициализация Bluedroid ---
    ESP_LOGI(TAG, "Initializing Bluedroid...");
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // --- Регистрация callback'ов ---
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);

    // --- Регистрация кастомного GATT сервиса ---
    ESP_LOGI(TAG, "Registering telemetry service...");
    ret = esp_ble_gatts_app_register(TELEMETRY_APP_ID);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT app register failed: %s", esp_err_to_name(ret));
        return;
    }

    // --- Инициализация HID устройства ---
    ESP_LOGI(TAG, "Initializing HID device...");
    
    // Объединяем все HID report descriptors
    static uint8_t combined_report_descriptor[512];
    uint16_t offset = 0;
    
    // Копируем keyboard report
    memcpy(combined_report_descriptor + offset, hid_report_keyboard, sizeof(hid_report_keyboard));
    offset += sizeof(hid_report_keyboard);
    
    // Копируем mouse report
    memcpy(combined_report_descriptor + offset, hid_report_mouse, sizeof(hid_report_mouse));
    offset += sizeof(hid_report_mouse);
    
    // Копируем gamepad report
    memcpy(combined_report_descriptor + offset, hid_report_gamepad, sizeof(hid_report_gamepad));
    offset += sizeof(hid_report_gamepad);

    esp_hid_raw_report_map_t report_map = {
        .len = offset,
        .data = combined_report_descriptor
    };

    esp_hid_device_config_t hid_config = {
        .report_maps = &report_map,
        .version = 0x0100
    };

    ret = esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE, hid_event_callback, &hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID device init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Telemetry and HID services initialized successfully");
}

void telemetry_start(void) {
    if (!data_queue) {
        data_queue = xQueueCreate(QUEUE_SIZE, 8);
    }
    
    xTaskCreate(telemetry_notify_task, "telemetry_notify_task", 4096, NULL, 5, NULL);
    xTaskCreate(data_generator_task, "data_generator_task", 4096, NULL, 5, NULL);

    // Начинаем рекламацию после инициализации всех сервисов
    ESP_LOGI(TAG, "Starting advertising...");
    esp_ble_gap_start_advertising(&adv_params);
}

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

// Удобная обёртка для отправки notify
esp_err_t telemetry_send(const uint8_t *data, uint16_t len) {
    if (!device_connected || !notifications_enabled || telemetry_gatts_if == ESP_GATT_IF_NONE) {
        return ESP_FAIL;
    }
    return esp_ble_gatts_send_indicate(
        telemetry_gatts_if,
        telemetry_conn_id,
        telemetry_handle_table[2], // TX value handle
        len,
        (uint8_t*)data,
        false
    );
}

// ---- Tasks ----
static void data_generator_task(void *arg) {
    uint8_t pkt[8];
    uint32_t counter = 0;
    const TickType_t xFrequency = pdMS_TO_TICKS(20); // 50 Hz
    while (1) {
        vTaskDelay(xFrequency);

        int16_t ax = 1000 + (counter % 100);
        int16_t ay = 1000 + (counter % 100);
        int16_t az = 1000 + (counter % 100);
        uint16_t angle = counter % 360;

        memcpy(&pkt[0], &ax, 2);
        memcpy(&pkt[2], &ay, 2);
        memcpy(&pkt[4], &az, 2);
        memcpy(&pkt[6], &angle, 2);
        counter++;

        if (device_connected) {
            (void)xQueueSend(data_queue, pkt, 0);
        }
    }
}

static void telemetry_notify_task(void *arg) {
    uint8_t data_to_send[8];
    uint32_t counter = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10)); // 100 Hz

        // Берём пакет из очереди, если есть; иначе генерируем "пилу"
        if (data_queue && xQueueReceive(data_queue, data_to_send, 0) != pdTRUE) {
            for (int i = 0; i < 8; i++) data_to_send[i] = (uint8_t)(counter + i);
        }

        if (device_connected && notifications_enabled) {
            esp_err_t ret = esp_ble_gatts_send_indicate(
                telemetry_gatts_if,
                telemetry_conn_id,
                telemetry_handle_table[2],
                sizeof(data_to_send),
                data_to_send,
                false
            );
            if (ret != ESP_OK) ESP_LOGE(TAG, "Notify failed: %s", esp_err_to_name(ret));
        }

        // HID: простая "демка" на основе данных
        if (hid_connected) {
            int8_t mx = ((int)data_to_send[0] - 128) / 10;
            int8_t my = ((int)data_to_send[1] - 128) / 10;
            send_mouse_report(mx, my, 0);

            send_gamepad_report(
                data_to_send[0],
                data_to_send[1],
                data_to_send[2],
                data_to_send[3],
                1 << (counter % 16)
            );

            if ((counter % 100) == 0) {
                // Нажимаем клавиши 'a'..'j' (HID keycodes 0x04..0x0D)
                send_keyboard_report(0x04 + (counter / 100) % 10);
            }
        }

        counter++;
    }
}