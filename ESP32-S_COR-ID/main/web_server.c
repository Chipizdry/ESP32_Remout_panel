#include "web_server.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include <string.h>
#include <sys/stat.h>
#include "esp_netif.h"
#include "esp_vfs.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "WebServer";
static httpd_handle_t server = NULL;

void wifi_init_sta(const char *ssid, const char *pass);
void wifi_init_ap(void);

static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);

static esp_err_t set_wifi_handler(httpd_req_t *req);

static void log_request_headers(httpd_req_t *req) {
    const char *headers_to_log[] = {
        "User-Agent",
        "Cookie",
        "Host",
        "Accept",
        "Connection",
        "Cache-Control",
        "Authorization",
        NULL
    };

    for (int i = 0; headers_to_log[i] != NULL; i++) {
        char buf[256];
        if (httpd_req_get_hdr_value_str(req, headers_to_log[i], buf, sizeof(buf)) == ESP_OK) {
            ESP_LOGI("HTTP_HEADERS", "%s: %s", headers_to_log[i], buf);
        }
    }
}



// Переключение между режимами
void switch_wifi_mode(wifi_mode_t mode, const char *ssid, const char *pass) {
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode == WIFI_MODE_STA) {
        wifi_init_sta(ssid, pass);
    } else if (mode == WIFI_MODE_AP) {
        wifi_init_ap();
    } else if (mode == WIFI_MODE_APSTA) {
        wifi_init_ap();
        wifi_init_sta(ssid, pass);
    }
}





static esp_err_t wildcard_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Wildcard redirect for URI: %s", req->uri);
    log_request_headers(req);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/main.html");
    httpd_resp_set_hdr(req, "Content-Length", "0");
    return httpd_resp_send(req, NULL, 0);
}

// Обработчик для Windows Connect Test (302 → main.html)
static esp_err_t windows_connect_test_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Windows connect test redirect → /main.html");
    log_request_headers(req);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/main.html");
    httpd_resp_set_hdr(req, "Content-Length", "0");
    return httpd_resp_send(req, NULL, 0);
}

// Обработчик для Microsoft NCSI
static esp_err_t ncsi_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Handling Microsoft NCSI request");
    const char *response = "Microsoft NCSI\nMicrosoft Connect Test";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "200 OK");
    return httpd_resp_send(req, response, strlen(response));
}

// Обработчик для Android Captive Portal
static esp_err_t android_captive_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Handling Android captive portal check - redirecting to main.html");
    log_request_headers(req); 
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/main.html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Content-Length", "0");
    return httpd_resp_send(req, NULL, 0);
}

// Обработчик для Apple Captive Portal
static esp_err_t apple_captive_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Redirecting Apple captive portal to /main.html");
    log_request_headers(req);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/main.html");
    return httpd_resp_send(req, NULL, 0);
}

// Обработчик /redirect (Windows иногда сам вызывает)
static esp_err_t redirect_fallback_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Handling /redirect → /main.html");
    log_request_headers(req);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/main.html");
    httpd_resp_set_hdr(req, "Content-Length", "0");
    return httpd_resp_send(req, NULL, 0);
}

// Основной обработчик статических файлов
static esp_err_t file_get_handler(httpd_req_t *req) {
    char filepath[512];
    const char *base = "/littlefs";

    if (strcmp(req->uri, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "%s/main.html", base);
    } else {
        strlcpy(filepath, base, sizeof(filepath));
        strlcat(filepath, req->uri, sizeof(filepath));
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    const char *ext = strrchr(filepath, '.');
    const char *content_type = "text/plain";
    if (ext) {
        if (strcmp(ext, ".html") == 0) content_type = "text/html";
        else if (strcmp(ext, ".css") == 0) content_type = "text/css";
        else if (strcmp(ext, ".js") == 0) content_type = "application/javascript";
    }

    httpd_resp_set_type(req, content_type);

    char buf[256];
    size_t read_len;
    while ((read_len = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_len) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}



static esp_err_t set_wifi_handler(httpd_req_t *req) {
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    char ssid[33] = {0}, pass[65] = {0}, mode_str[16] = {0};
    cJSON *json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    cJSON *js_ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *js_pass = cJSON_GetObjectItem(json, "password");
    cJSON *js_mode = cJSON_GetObjectItem(json, "mode");

    if (cJSON_IsString(js_ssid) && cJSON_IsString(js_pass) && cJSON_IsString(js_mode)) {
        strncpy(ssid, js_ssid->valuestring, sizeof(ssid)-1);
        strncpy(pass, js_pass->valuestring, sizeof(pass)-1);
        strncpy(mode_str, js_mode->valuestring, sizeof(mode_str)-1);

        wifi_mode_t mode = WIFI_MODE_STA;
        if (strcmp(mode_str, "STA") == 0) mode = WIFI_MODE_STA;
        else if (strcmp(mode_str, "AP") == 0) mode = WIFI_MODE_AP;
        else if (strcmp(mode_str, "STA_AP") == 0) mode = WIFI_MODE_APSTA;

        // Сохраняем в NVS
        nvs_handle_t nvs;
        if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "ssid", ssid);
            nvs_set_str(nvs, "pass", pass);
            nvs_set_str(nvs, "mode", mode_str);
            nvs_commit(nvs);
            nvs_close(nvs);
        }

        switch_wifi_mode(mode, ssid, pass);

        httpd_resp_sendstr(req, "Wi-Fi settings saved and mode switched");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    cJSON_Delete(json);
    return ESP_OK;
}

// Обработчик событий Wi-Fi и IP
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
if (event_base == WIFI_EVENT) {
if (event_id == WIFI_EVENT_STA_START) {
esp_wifi_connect();
ESP_LOGI(TAG, "STA started, connecting...");
} else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
ESP_LOGW(TAG, "STA disconnected, retrying...");
esp_wifi_connect();
}
} 
else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
}
}


// Запуск станции
void wifi_init_sta(const char *ssid, const char *pass) {
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char*)sta_config.sta.password, pass, sizeof(sta_config.sta.password));

    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}


// Запуск точки доступа
void wifi_init_ap(void) {
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_AP",
            .ssid_len = strlen("ESP32_AP"),
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: SSID:%s, PASS:%s", ap_config.ap.ssid, ap_config.ap.password);
}



void start_webserver(void) {
    if (server) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
   
    config.server_port = 80;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.max_resp_headers = 16;
    config.max_open_sockets = 7;
    config.backlog_conn = 5;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server");
        return;
    }

    // Зарегистрировать все URI обработчики
    httpd_uri_t handlers[] = {
        // Captive portal (Windows, Android, Apple)
        { .uri = "/connecttest.txt",     .method = HTTP_GET, .handler = windows_connect_test_handler },
        { .uri = "/ncsi.txt",            .method = HTTP_GET, .handler = ncsi_handler },
        { .uri = "/generate_204",        .method = HTTP_GET, .handler = android_captive_handler },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = apple_captive_handler },
        { .uri = "/redirect",            .method = HTTP_GET, .handler = redirect_fallback_handler },

        // Основные ресурсы
        { .uri = "/",           .method = HTTP_GET, .handler = file_get_handler },
        { .uri = "/main.html",  .method = HTTP_GET, .handler = file_get_handler },
        { .uri = "/style.css",  .method = HTTP_GET, .handler = file_get_handler },
        { .uri = "/script.js",  .method = HTTP_GET, .handler = file_get_handler },
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        if (httpd_register_uri_handler(server, &handlers[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register handler for %s", handlers[i].uri);
        }
    }


    httpd_uri_t catch_all_handler = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = wildcard_handler,
        .user_ctx = NULL
    };

    if (httpd_register_uri_handler(server, &catch_all_handler) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register wildcard handler");
    }

    httpd_uri_t set_wifi_uri = {
        .uri = "/set_wifi",
        .method = HTTP_POST,
        .handler = set_wifi_handler
    };
    httpd_register_uri_handler(server, &set_wifi_uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
}


