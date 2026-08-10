#pragma once

#include <stdint.h>

#include "telemetry.h"

enum telemetry_source_type {
	TELEMETRY_SOURCE_TEMPERATURE,
	TELEMETRY_SOURCE_IMU,
};

/* 联合体中只存放一种数据源对应的一条样本。 */
union telemetry_source_data {
	struct telemetry_temperature temperature;
	struct telemetry_imu imu;
};

/* 每个数据源都暴露一次读取方法和自身的配置采样周期。 */
struct telemetry_source {
	enum telemetry_source_type type;
	/* 返回配置的采样周期，单位为秒。 */
	uint32_t (*interval)(void);
	/* 读取一条样本；失败时返回负 errno 值。 */
	int (*get_data)(union telemetry_source_data *data);
};

extern const struct telemetry_source telemetry_temperature_source;
extern const struct telemetry_source telemetry_imu_source;
