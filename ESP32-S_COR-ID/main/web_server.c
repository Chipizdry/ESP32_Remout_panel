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

// --- Captive Portal Redirect (HTTP 302) ---
static esp_err_t redirect_handler(httpd_req_t *req)
{
    ESP_LOGW("WebServer", "Redirecting %s → /main.html", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/main.html");
    httpd_resp_set_hdr(req, "Content-Length", "0");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// --- Static File Server Handler ---
static esp_err_t file_get_handler(httpd_req_t *req)
{
    char filepath[512];
    const char *base = "/littlefs";

    // Проверка на слишком длинный URI
    if (strlen(req->uri) > 255) {
        ESP_LOGE("WebServer", "URI too long: %s", req->uri);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "URI too long");
    }

    // root or main.html
    if (strcmp(req->uri, "/") == 0 || strcmp(req->uri, "/main.html") == 0) {
        snprintf(filepath, sizeof(filepath), "%s/main.html", base);
    } else {
        // Жёстко ограничим длину URI, чтобы не переполнить filepath
        size_t uri_max = sizeof(filepath) - strlen(base) - 1;
        if (strlen(req->uri) > uri_max) {
            ESP_LOGE(TAG, "URI too long: %s", req->uri);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "URI too long");
        }

        // Безопасное объединение пути
        strcpy(filepath, base);
        strncat(filepath, req->uri, uri_max);
    }
    ESP_LOGI(TAG, "Serving file: %s", filepath);
    // Открываем файл
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGW("WebServer", "File not found: %s → redirect", filepath);
        return redirect_handler(req);  // редирект на main.html
    }

    // Определение MIME-типа
    const char *ct = "text/plain";
    if (strstr(filepath, ".html"))      ct = "text/html";
    else if (strstr(filepath, ".css"))  ct = "text/css";
    else if (strstr(filepath, ".js"))   ct = "application/javascript";
    else if (strstr(filepath, ".png"))  ct = "image/png";
    else if (strstr(filepath, ".ico"))  ct = "image/x-icon";

    httpd_resp_set_type(req, ct);

    // Отправка файла порциями
    char buf[256];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, r);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

void start_webserver(void)
{
    static httpd_handle_t server = NULL;
    if (server) {
        ESP_LOGW("WebServer", "Already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE("WebServer", "Failed to start server");
        return;
    }

    // 1. Captive Portal detection URLs → redirect_handler
    const char *cp_uris[] = {
        "/generate_204",         // Android
        "/hotspot-detect.html",  // Apple
        "/ncsi.txt"              // Windows
    };
    for (int i = 0; i < sizeof(cp_uris) / sizeof(cp_uris[0]); i++) {
        httpd_uri_t uri = {
            .uri     = cp_uris[i],
            .method  = HTTP_GET,
            .handler = redirect_handler
        };
        httpd_register_uri_handler(server, &uri);
    }

    // 2. Основные ресурсы (включая "/main.html", "/")
    const char *static_uris[] = {
        "/", "/main.html", "/style.css", "/script.js"
    };
    for (int i = 0; i < sizeof(static_uris) / sizeof(static_uris[0]); i++) {
        httpd_uri_t uri = {
            .uri     = static_uris[i],
            .method  = HTTP_GET,
            .handler = file_get_handler
        };
        httpd_register_uri_handler(server, &uri);
    }

    // 3. Catch-all — всё остальное → file_get_handler (с редиректом внутри)
    httpd_uri_t all = {
        .uri     = "/*",
        .method  = HTTP_GET,
        .handler = file_get_handler
    };
    httpd_register_uri_handler(server, &all);

    ESP_LOGI("WebServer", "HTTP server started on port %d", config.server_port);
}

void stop_webserver(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

void register_api_handler(httpd_uri_t *uri)
{
    if (server) {
        httpd_register_uri_handler(server, uri);
    }
}


