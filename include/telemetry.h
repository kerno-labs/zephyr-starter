#pragma once

#include <stddef.h>
#include <stdint.h>

/* 批大小来自 Kconfig，同时决定 nanopb 生成静态数组的容量。 */
#define TELEMETRY_TEMPERATURE_SAMPLES CONFIG_APP_TEMPERATURE_BATCH_SIZE
#define TELEMETRY_IMU_SAMPLES CONFIG_APP_IMU_BATCH_SIZE
