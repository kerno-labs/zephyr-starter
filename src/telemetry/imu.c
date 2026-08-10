/* CoreS3 BMI270 六轴数据源实现。 */
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "telemetry/source.h"

LOG_MODULE_REGISTER(telemetry_imu, LOG_LEVEL_INF);

static const struct device *const imu = DEVICE_DT_GET(DT_ALIAS(accel0));
static bool imu_configured;

static int configure(void)
{
	/* BMI270 驱动默认不启动加速度计和陀螺仪，必须设置输出速率以启用它们。 */
	const struct sensor_value output_rate = { .val1 = 25, .val2 = 0 };

	if (imu_configured) {
		return 0;
	}
	if (!device_is_ready(imu)) {
		return -ENODEV;
	}
	if (sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
			    SENSOR_ATTR_SAMPLING_FREQUENCY, &output_rate) != 0 ||
	    sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ,
			    SENSOR_ATTR_SAMPLING_FREQUENCY, &output_rate) != 0) {
		return -EIO;
	}
	imu_configured = true;
	LOG_INF("BMI270 加速度计和陀螺仪已启用，输出速率 25 Hz");
	return 0;
}

static int32_t milli(const struct sensor_value *value)
{
	/* 将 Zephyr 的 SI sensor_value 转为 ×1000 固定小数。 */
	return (int32_t)(sensor_value_to_micro(value) / 1000);
}

static uint32_t interval(void)
{
	/* 返回配置的 IMU 采样周期，单位为秒。 */
	return CONFIG_APP_IMU_SAMPLE_INTERVAL_S;
}

static int get_data(union telemetry_source_data *data)
{
	/* 获取一组一致的 BMI270 数据，并将六轴全部转换为 ×1000 单位。 */
	struct sensor_value accel[3];
	struct sensor_value gyro[3];

	if (configure() != 0 || sensor_sample_fetch(imu) != 0 ||
	    sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel) != 0 ||
	    sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro) != 0) {
		return -EIO;
	}
	data->imu = (struct telemetry_imu){
		.accel_x = milli(&accel[0]), .accel_y = milli(&accel[1]), .accel_z = milli(&accel[2]),
		.gyro_x = milli(&gyro[0]), .gyro_y = milli(&gyro[1]), .gyro_z = milli(&gyro[2]),
	};
	return 0;
}

const struct telemetry_source telemetry_imu_source = {
	/* 通过面向 collector 的统一接口导出该数据源。 */
	.type = TELEMETRY_SOURCE_IMU,
	.interval = interval,
	.get_data = get_data,
};
