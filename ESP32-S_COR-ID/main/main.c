#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_littlefs.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include <dirent.h> 
#include <sys/stat.h>
#include "web_server.h"
#include "i2c_peripheral.h"
#include "ads1115_reader.h"
#include "dns_server.h"
#include "rgb_led.h"

#include "esp_system.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "telemetry.h" 

#include "cJSON.h"

char ssid[33], pass[65];
size_t len;
nvs_handle_t nvs;
bool creds_found = false;

#define AP_SSID      "COR-Velo"
#define AP_PASSWORD  "password123"
#define AP_CHANNEL   6
#define MAX_CONN     8

#define BASE_PATH "/littlefs"
#define PARTITION_LABEL "web" 
static const char *TAG = "WiFi Mode Switch";


extern void telemetry_init(void);

extern void telemetry_start(void);

void init_littlefs();
void check_files();



void app_main() {
    // Инициализация NVS (обязательно для Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализация Wi-Fi и событий
    ESP_ERROR_CHECK(esp_netif_init());
ESP_ERROR_CHECK(esp_event_loop_create_default());

// Создай netif один раз (можно создать сразу оба, если потом планируется AP<->STA переключение)
esp_netif_t *ap_netif  = esp_netif_create_default_wifi_ap();
esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_wifi_init(&cfg));

// Зарегистрируй обработчики СРАЗУ ЗДЕСЬ, один раз
ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

// Дальше — твоя логика выбора режима (AP или STA) без повторных init/create
if (creds_found) {
    switch_wifi_mode(WIFI_MODE_STA, ssid, pass);
} else {
    switch_wifi_mode(WIFI_MODE_AP, NULL, NULL);
}


    // Старт в режиме AP при загрузке
  //  wifi_init_ap();
    init_littlefs();
    check_files();
    start_webserver();
    ESP_ERROR_CHECK(i2c_master_init());
    init_rgb_pwm();

    ESP_LOGI("MAIN", "Free heap: %" PRIu32, esp_get_free_heap_size());

    telemetry_init();
     
    telemetry_start();
  // Сканирование I2C-шины
  uint8_t found_devices[10];
  int device_count = i2c_scan(found_devices, 10);
  for (int i = 0; i < device_count; i++) {
      ESP_LOGI("I2C", "Устройство %d: адрес 0x%02X", i + 1, found_devices[i]);
  }

    ads1115_reader_start();
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL);
}



void init_littlefs() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = PARTITION_LABEL,
        .format_if_mount_failed = true,
                .dont_mount = false
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(PARTITION_LABEL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
}

void check_files() {
    ESP_LOGI(TAG, "Files in %s:", BASE_PATH);
    
    DIR *dir = opendir(BASE_PATH);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            ESP_LOGI(TAG, "- %s", entry->d_name);
        }
    }
    
    closedir(dir);
}