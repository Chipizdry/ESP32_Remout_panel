

#ifdef CONFIG_BT_ENABLED

#include "bt_hid.h"
#include "esp_log.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hid_common.h"
#include <stddef.h>  // Для size_t
#include <stdbool.h> 

static const char *BT_TAG = "BT_HID";
static esp_hidh_dev_t *hid_device = NULL;
static bool is_connected = false;

static void hidh_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
        case ESP_HIDH_OPEN_EVENT: {
            if (param->open.status == ESP_OK) {
                ESP_LOGI(BT_TAG, "HID Device Opened");
                hid_device = param->open.dev;
                is_connected = true;
            } else {
                ESP_LOGE(BT_TAG, "HID Device Open Failed");
            }
            break;
        }
        case ESP_HIDH_CLOSE_EVENT: {
            ESP_LOGI(BT_TAG, "HID Device Closed");
            is_connected = false;
            hid_device = NULL;
            break;
        }
        case ESP_HIDH_INPUT_EVENT: {
            ESP_LOGI(BT_TAG, "Input Report, Map: %d, ID: %d, Size: %d", 
                    param->input.map_index, param->input.report_id, param->input.length);
            // Здесь можно обрабатывать входящие данные
            break;
        }
        case ESP_HIDH_FEATURE_EVENT: {
            ESP_LOGI(BT_TAG, "Feature Report, Map: %d, ID: %d, Size: %d", 
                    param->feature.map_index, param->feature.report_id, param->feature.length);
            break;
        }
        default:
            break;
    }
}

static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            ESP_LOGI(BT_TAG, "Device discovered: %s", param->disc_res.bda);
            // Здесь можно фильтровать устройства и подключаться к нужному
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(BT_TAG, "Discovery stopped");
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(BT_TAG, "Discovery started");
            }
            break;
        }
        default:
            break;
    }
}

void bt_hid_init(void)
{
    // Инициализация Bluetooth контроллера
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
        ESP_LOGE(BT_TAG, "Failed to initialize controller");
        return;
    }

    if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
        ESP_LOGE(BT_TAG, "Failed to enable controller");
        return;
    }

    if (esp_bluedroid_init() != ESP_OK) {
        ESP_LOGE(BT_TAG, "Failed to initialize bluedroid");
        return;
    }

    if (esp_bluedroid_enable() != ESP_OK) {
        ESP_LOGE(BT_TAG, "Failed to enable bluedroid");
        return;
    }

    // Установка имени устройства
    esp_bt_dev_set_device_name("ESP32 HID Host");

    // Регистрация callback для GAP событий
    esp_bt_gap_register_callback(gap_callback);

    // Инициализация HID хоста
    esp_hidh_config_t config = {
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = NULL
    };

    if (esp_hidh_init(&config) != ESP_OK) {
        ESP_LOGE(BT_TAG, "Failed to initialize HID host");
        return;
    }

    ESP_LOGI(BT_TAG, "Bluetooth HID initialized");
}

void bt_hid_start_discovery(void)
{
    // Начало поиска HID устройств
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
}

void bt_hid_send_data(uint8_t *data, size_t length)
{
    if (!is_connected || hid_device == NULL) {
        ESP_LOGE(BT_TAG, "No HID device connected");
        return;
    }

    // Отправка данных через HID
    esp_err_t ret = esp_hidh_dev_output_report(hid_device, 0, data, length);
    if (ret != ESP_OK) {
        ESP_LOGE(BT_TAG, "Failed to send HID data: %s", esp_err_to_name(ret));
    }
}

bool bt_hid_is_connected(void)
{
    return is_connected;
}

#else // CONFIG_BT_ENABLED

#include "bt_hid.h"
#include "esp_log.h"
#include <stddef.h>  
#include <stdbool.h> 
#include <stdint.h> 

void bt_hid_init(void) {
    ESP_LOGW("BT_HID", "Bluetooth is disabled in configuration");
}

void bt_hid_start_discovery(void) {
    ESP_LOGW("BT_HID", "Bluetooth is disabled in configuration");
}

void bt_hid_send_data(uint8_t *data, size_t length) {
    ESP_LOGW("BT_HID", "Bluetooth is disabled in configuration");
}

bool bt_hid_is_connected(void) {
    return false;
}

#endif // CONFIG_BT_ENABLED