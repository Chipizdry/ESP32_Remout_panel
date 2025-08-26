


#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"
#include "esp_wifi.h"
void start_webserver(void);
void stop_webserver(void);


void wifi_init_sta(const char *ssid, const char *pass);
void wifi_init_ap(void);


void switch_wifi_mode(wifi_mode_t mode, const char *sta_ssid, const char *sta_password);
esp_err_t send_html_response(httpd_req_t *req, const char* html_path);
void register_api_handler(httpd_uri_t *uri);

#endif