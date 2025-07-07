

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include <string.h>

static const char *TAG = "dns_server";

#define DNS_PORT 53
#define DNS_RESPONSE_IP "192.168.4.1"

void dns_server_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);

    uint8_t buffer[512];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int len = recvfrom(sock, buffer, sizeof(buffer), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < 0) continue;

        // Простейшая реализация: копируем запрос, меняем флаг ответа и IP
        buffer[2] |= 0x80; // Ответ
        buffer[3] |= 0x80; // Авторитетный

        // Количество ответов = 1
        buffer[6] = 0;
        buffer[7] = 1;

        // Указатель на конец запроса
        int query_len = len;

        // Добавим ответ
        buffer[query_len++] = 0xC0; buffer[query_len++] = 0x0C; // pointer to domain name
        buffer[query_len++] = 0x00; buffer[query_len++] = 0x01; // type A
        buffer[query_len++] = 0x00; buffer[query_len++] = 0x01; // class IN
        buffer[query_len++] = 0x00; buffer[query_len++] = 0x00; buffer[query_len++] = 0x00; buffer[query_len++] = 0x3C; // TTL
        buffer[query_len++] = 0x00; buffer[query_len++] = 0x04; // data length = 4

        struct in_addr addr;
        inet_pton(AF_INET, DNS_RESPONSE_IP, &addr);
        memcpy(&buffer[query_len], &addr.s_addr, 4);
        query_len += 4;

        sendto(sock, buffer, query_len, 0, (struct sockaddr *)&client_addr, client_len);
    }
}

