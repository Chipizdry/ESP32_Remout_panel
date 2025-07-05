


#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"

void start_webserver(void);
void stop_webserver(void);
esp_err_t send_html_response(httpd_req_t *req, const char* html_path);
void register_api_handler(httpd_uri_t *uri);

#endif