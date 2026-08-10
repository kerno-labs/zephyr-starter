cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_starter)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
  src/main.c
  src/wifi.c
  src/telemetry/mcu_temperature.c
  src/telemetry/imu.c
  src/time_service.c
  src/mqtt_uploader.c
)
zephyr_nanopb_sources(app proto/telemetry.proto)

# app.local.conf 提供相对路径。CA 证书在构建时嵌入固件；
# 裸机运行时不存在可读取的本地文件路径。
if(DEFINED EXTRA_CONF_FILE AND EXISTS "${EXTRA_CONF_FILE}")
  file(STRINGS "${EXTRA_CONF_FILE}" app_ca_line REGEX "^CONFIG_APP_MQTT_CA_CERT_PATH=\"")
  if(app_ca_line MATCHES "^CONFIG_APP_MQTT_CA_CERT_PATH=\"([^\"]+)\"")
    set(app_ca_file "${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_MATCH_1}")
    if(EXISTS "${app_ca_file}")
      generate_inc_file_for_target(app "${app_ca_file}" "${CMAKE_CURRENT_BINARY_DIR}/app_ca_cert.inc")
      target_compile_definitions(app PRIVATE APP_HAS_CA_CERT=1)
    else()
      message(FATAL_ERROR "CONFIG_APP_MQTT_CA_CERT_PATH does not exist: ${app_ca_file}")
    endif()
  endif()
endif()
