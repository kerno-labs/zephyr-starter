#pragma once

#include <stddef.h>
#include <stdint.h>

/* 批大小来自 Kconfig，同时决定 nanopb 生成静态数组的容量。 */
#define TELEMETRY_TEMPERATURE_SAMPLES CONFIG_APP_TEMPERATURE_BATCH_SIZE
#define TELEMETRY_IMU_SAMPLES CONFIG_APP_IMU_BATCH_SIZE

/* 单条 MCU 温度样本，单位为摄氏度 × 1000。 */
struct telemetry_temperature {
	int32_t value;
};

/* 单条 IMU 样本，单位与 telemetry.proto 中的固定小数说明一致。 */
struct telemetry_imu {
	int32_t accel_x;
	int32_t accel_y;
	int32_t accel_z;
	int32_t gyro_x;
	int32_t gyro_y;
	int32_t gyro_z;
};

/* 将一个完整 MCU 温度批次编码为单条 PropertyPost protobuf payload。 */
int telemetry_encode_temperature(uint64_t time_ms, const int32_t values[], size_t count,
				 uint8_t *out, size_t out_size, size_t *encoded_size);

/* 将一个完整 IMU 批次编码为单条 PropertyPost protobuf payload。 */
int telemetry_encode_imu(uint64_t time_ms, const struct telemetry_imu values[], size_t count,
			 uint8_t *out, size_t out_size, size_t *encoded_size);
