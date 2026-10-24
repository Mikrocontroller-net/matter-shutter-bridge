cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
  message(FATAL_ERROR "ESP_MATTER_PATH is not set; run: source tools/env.sh")
endif()

set(PROJECT_VER "0.1")
set(PROJECT_VER_NUMBER 1)

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# esp-matter's device HAL is not used: main/ drives GPIO and RMT itself.
set(EXTRA_COMPONENT_DIRS
  "${MATTER_SDK_PATH}/config/esp32/components"
  "${ESP_MATTER_PATH}/components")

project(matter-shutter-bridge)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security;-Wno-error=cpp" APPEND)
