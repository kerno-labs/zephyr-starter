/* CoreS3 MCU 芯片温度数据源实现。 */
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include "telemetry/source.h"

static const struct device *const die_temp = DEVICE_DT_GET(DT_ALIAS(die_temp0));

static uint32_t interval(void)
{
	/* 返回秒数，与 collector 中的 k_work_schedule(K_SECONDS(...)) 对应。 */
	return CONFIG_APP_TEMPERATURE_SAMPLE_INTERVAL_S;
}

static int get_data(union telemetry_source_data *data)
{
	/* 读取芯片温度，并将 Zephyr sensor_value 转为摄氏度 × 1000。 */
	struct sensor_value value;

	if (!device_is_ready(die_temp)) {
		return -ENODEV;
	}
	if (sensor_sample_fetch(die_temp) != 0 ||
	    sensor_channel_get(die_temp, SENSOR_CHAN_DIE_TEMP, &value) != 0) {
		return -EIO;
	}
	data->temperature.value = (int32_t)(sensor_value_to_micro(&value) / 1000);
	return 0;
}

const struct telemetry_source telemetry_temperature_source = {
	/* 通过面向 collector 的统一接口导出该数据源。 */
	.type = TELEMETRY_SOURCE_TEMPERATURE,
	.interval = interval,
	.get_data = get_data,
};
