


#include "web_server.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "WebServer";
static httpd_handle_t server = NULL;

// Обработчик статических файлов
static esp_err_t file_get_handler(httpd_req_t *req) {
    char filepath[1024];
    const char *base_path = "/littlefs";
    
    // Проверка длины URI перед формированием пути
    if (strlen(req->uri) > 1000) {  // Защита от слишком длинных URI
        ESP_LOGE(TAG, "URI too long: %s", req->uri);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Определяем запрашиваемый файл
    if (strcmp(req->uri, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "%s/main.html", base_path);
    } else {
        snprintf(filepath, sizeof(filepath), "%s%s", base_path, req->uri);
    }

    // Определяем Content-Type
    const char *content_type;
    if (strstr(filepath, ".html")) content_type = "text/html";
    else if (strstr(filepath, ".css")) content_type = "text/css";
    else if (strstr(filepath, ".js")) content_type = "application/javascript";
    else if (strstr(filepath, ".png")) content_type = "image/png";
    else content_type = "text/plain";

    // Открываем файл
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    // Отправляем файл
    httpd_resp_set_type(req, content_type);
    
    char buf[256];
    size_t read;
    do {
        read = fread(buf, 1, sizeof(buf), file);
        if (httpd_resp_send_chunk(req, buf, read) != ESP_OK) {
            fclose(file);
            return ESP_FAIL;
        }
    } while (read > 0);
    
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

void start_webserver(void) {
    if (server) {
        ESP_LOGE(TAG, "Web server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 7;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Регистрируем обработчики
        httpd_uri_t uri_root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = file_get_handler,
            .user_ctx = NULL
        };


        httpd_uri_t uri_css = {
            .uri = "/style.css",
            .method = HTTP_GET,
            .handler = file_get_handler,
            .user_ctx = NULL
        };
        
        httpd_uri_t uri_js = {
            .uri = "/script.js",
            .method = HTTP_GET,
            .handler = file_get_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(server, &uri_css);
        httpd_register_uri_handler(server, &uri_js); 
        httpd_register_uri_handler(server, &uri_root);
        
        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    }

    
}

void stop_webserver(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

void register_api_handler(httpd_uri_t *uri) {
    if (server) {
        httpd_register_uri_handler(server, uri);
    }
}