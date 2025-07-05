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

#define AP_SSID      "ESP32_AP"
#define AP_PASSWORD  "password123"
#define AP_CHANNEL   6
#define MAX_CONN     8

#define BASE_PATH "/littlefs"
#define PARTITION_LABEL "web" 
static const char *TAG = "WiFi Mode Switch";

void init_littlefs();
void check_files();

void wifi_init_ap() {
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .password = AP_PASSWORD,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .max_connection = MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP mode started. SSID: %s", AP_SSID);
}

void switch_wifi_mode(wifi_mode_t mode, const char *sta_ssid, const char *sta_password) {
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_config_t sta_config = {
            .sta = {
                .threshold = {
                    .authmode = WIFI_AUTH_WPA2_PSK
                }
            }
        };
        strncpy((char*)sta_config.sta.ssid, sta_ssid, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, sta_password, sizeof(sta_config.sta.password));
        
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Switched to mode: %d", mode);
}

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
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Старт в режиме AP при загрузке
    wifi_init_ap();
    init_littlefs();
    check_files();
    start_webserver();
    ESP_ERROR_CHECK(i2c_master_init());

  // Сканирование I2C-шины
  uint8_t found_devices[10];
  int device_count = i2c_scan(found_devices, 10);
  for (int i = 0; i < device_count; i++) {
      ESP_LOGI("I2C", "Устройство %d: адрес 0x%02X", i + 1, found_devices[i]);
  }

    ads1115_reader_start();
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