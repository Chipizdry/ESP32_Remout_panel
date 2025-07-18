# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/Espressif/frameworks/esp-idf-v5.3.1/v5.3.1/esp-idf/components/bootloader/subproject"
  "D:/GitHub/ESP32_Remout_panel/ESP32-S_COR-ID/build/bootloader"
  "D:/GitHub/ESP32_Remout_panel/ESP32-S_COR-ID/build/bootloader-prefix"
  "D:/GitHub/ESP32_Remout_panel/ESP32-S_COR-ID/build/bootloader-prefix/tmp"
  "D:/GitHub/ESP32_Remout_panel/ESP32-S_COR-ID/build/bootloader-prefix/src/bootloader-stamp"
  "D:/GitHub/ESP32_Remout_panel/ESP32-S_COR-ID/build/bootloader-prefix/src"
  "D:/GitHub/ESP32_Remout_panel/ESP32-S_COR-ID/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/GitHub/ESP32_Remout_panel/ESP32-S_COR-ID/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/GitHub/ESP32_Remout_panel/ESP32-S_COR-ID/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
