#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <pb_encode.h>
#include "proto/telemetry.pb.h"

#include "mqtt_uploader.h"
#include "telemetry.h"
#include "telemetry/register.h"
#include "time_service.h"

LOG_MODULE_REGISTER(telemetry_imu, LOG_LEVEL_INF);

static const struct device *const imu = DEVICE_DT_GET(DT_ALIAS(accel0));
struct telemetry_imu {
	int32_t accel_x;
	int32_t accel_y;
	int32_t accel_z;
	int32_t gyro_x;
	int32_t gyro_y;
	int32_t gyro_z;
};

static struct telemetry_imu samples[TELEMETRY_IMU_SAMPLES];
static size_t sample_count;
static uint64_t batch_time_ms;

static int prepare(void)
{
	const struct sensor_value rate = { .val1 = 25 };

	if (!device_is_ready(imu) ||
	    sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &rate) != 0 ||
	    sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &rate) != 0) {
		return -EIO;
	}
	return 0;
}

static uint32_t interval(void)
{
	return CONFIG_APP_IMU_SAMPLE_INTERVAL_S;
}

static int process(void)
{
	uint8_t buffer[CONFIG_APP_MQTT_BUFFER_SIZE];
	pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
	starter_iot_telemetry_v1_PropertyPost post = starter_iot_telemetry_v1_PropertyPost_init_zero;

	post.id = sys_rand32_get();
	strcpy(post.version, "1.0");
	post.method = starter_iot_telemetry_v1_Method_PROPERTY_POST;
	post.params_count = 1;
	post.params[0].time_ms = batch_time_ms;
	post.params[0].which_value = starter_iot_telemetry_v1_Property_imu_tag;
	post.params[0].value.imu.samples_count = sample_count;
	for (size_t i = 0; i < sample_count; ++i) {
		starter_iot_telemetry_v1_Imu *out = &post.params[0].value.imu.samples[i];
		*out = (starter_iot_telemetry_v1_Imu){ samples[i].accel_x, samples[i].accel_y, samples[i].accel_z, samples[i].gyro_x, samples[i].gyro_y, samples[i].gyro_z };
	}
	return pb_encode(&stream, starter_iot_telemetry_v1_PropertyPost_fields, &post)
		? mqtt_uploader_enqueue(buffer, stream.bytes_written) : -EIO;
}

static int flush(void)
{
	int rc;

	if (sample_count == 0) {
		return 0;
	}
	rc = process();
	if (rc == 0) {
		LOG_INF("IMU batch queued: %u samples", (unsigned int)sample_count);
		sample_count = 0;
	}
	return rc;
}

static int32_t milli(const struct sensor_value *value)
{
	return (int32_t)(sensor_value_to_micro(value) / 1000);
}

static int collect_data(void)
{
	struct sensor_value accel[3];
	struct sensor_value gyro[3];
	uint64_t time_ms;

	if (sample_count == ARRAY_SIZE(samples) && flush() != 0) {
		return -EIO;
	}
	if (!time_service_get_ms(&time_ms) || sensor_sample_fetch(imu) != 0 ||
	    sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel) != 0 ||
	    sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro) != 0) {
		return -EIO;
	}
	if (sample_count == 0) {
		batch_time_ms = time_ms;
	}
	samples[sample_count++] = (struct telemetry_imu){
		.accel_x = milli(&accel[0]),
		.accel_y = milli(&accel[1]),
		.accel_z = milli(&accel[2]),
		.gyro_x = milli(&gyro[0]),
		.gyro_y = milli(&gyro[1]),
		.gyro_z = milli(&gyro[2]),
	};
	LOG_INF("IMU: acc=(%d,%d,%d), gyro=(%d,%d,%d), batch: %u/%u",
		samples[sample_count - 1].accel_x, samples[sample_count - 1].accel_y,
		samples[sample_count - 1].accel_z, samples[sample_count - 1].gyro_x,
		samples[sample_count - 1].gyro_y, samples[sample_count - 1].gyro_z,
		(unsigned int)sample_count, (unsigned int)ARRAY_SIZE(samples));
	return sample_count == ARRAY_SIZE(samples) ? flush() : 0;
}

static void work_handler(struct k_work *work)
{
	if (collect_data() != 0) {
		LOG_WRN("IMU collection failed");
	}
	k_work_schedule(k_work_delayable_from_work(work), K_SECONDS(interval()));
}

static K_WORK_DELAYABLE_DEFINE(work, work_handler);

void telemetry_imu_register(void)
{
	if (prepare() != 0) {
		LOG_WRN("IMU preparation failed");
		return;
	}
	k_work_schedule(&work, K_NO_WAIT);
}
