idf_component_register(
    SRCS "dns_server.c" "ads1115_reader.c" "main.c" "web_server.c" "i2c_peripheral.c"   "ads1115_reader.c"  "dns_server.c"
    INCLUDE_DIRS "."
    REQUIRES 
    esp_littlefs
    esp_wifi
    nvs_flash
    esp_netif
    esp_event
    driver
    esp_timer
    esp_http_server
    esp_event lwip
)
include_directories(
    "D:/Espressif/frameworks/esp-idf-v5.3.1/v5.3.1/esp-idf/components/esp_littlefs/src/littlefs"
    
)

littlefs_create_partition_image(
    web  # Должно совпадать с именем раздела из partitions.csv
    ${CMAKE_CURRENT_SOURCE_DIR}/web_content  # Путь к файлам
    FLASH_IN_PROJECT 
)
                    
set(PARTITION_TABLE csv)
set(PARTITION_TABLE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv")
