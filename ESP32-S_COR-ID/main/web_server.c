#include "web_server.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include <string.h>
#include <sys/stat.h>
#include "esp_netif.h"
#include "esp_vfs.h"
#include "lwip/sockets.h"

static const char *TAG = "WebServer";
static httpd_handle_t server = NULL;

// Обработчик для Windows Connect Test (302 → main.html)
static esp_err_t windows_connect_test_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Windows connect test redirect → /main.html");
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
    ESP_LOGI(TAG, "Handling Android captive portal check");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// Обработчик для Apple Captive Portal
static esp_err_t apple_captive_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Redirecting Apple captive portal to /main.html");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/main.html");
    return httpd_resp_send(req, NULL, 0);
}

// Обработчик /redirect (Windows иногда сам вызывает)
static esp_err_t redirect_fallback_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Handling /redirect → /main.html");
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

void start_webserver(void) {
    if (server) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;

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

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
}
