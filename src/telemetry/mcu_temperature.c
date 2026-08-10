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

LOG_MODULE_REGISTER(telemetry_temperature, LOG_LEVEL_INF);

static const struct device *const die_temp = DEVICE_DT_GET(DT_ALIAS(die_temp0));
struct telemetry_temperature {
	int32_t value;
};
static struct telemetry_temperature samples[TELEMETRY_TEMPERATURE_SAMPLES];
static size_t sample_count;
static uint64_t batch_time_ms;

static int prepare(void)
{
	return device_is_ready(die_temp) ? 0 : -ENODEV;
}

static uint32_t interval(void)
{
	return CONFIG_APP_TEMPERATURE_SAMPLE_INTERVAL_S;
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
	post.params[0].which_value = starter_iot_telemetry_v1_Property_cpu_temperature_tag;
	post.params[0].value.cpu_temperature.samples_count = sample_count;
	for (size_t i = 0; i < sample_count; ++i) {
		post.params[0].value.cpu_temperature.samples[i].value = samples[i].value;
	}
	return pb_encode(&stream, starter_iot_telemetry_v1_PropertyPost_fields, &post) ? mqtt_uploader_enqueue(buffer, stream.bytes_written) : -EIO;
}

static int flush(void)
{
	int rc;

	if (sample_count == 0) {
		return 0;
	}
	rc = process();
	if (rc == 0) {
		LOG_INF("MCU temperature batch queued: %u samples", (unsigned int)sample_count);
		sample_count = 0;
	}
	return rc;
}

static int collect_data(void)
{
	struct sensor_value value;
	uint64_t time_ms;

	if (sample_count == ARRAY_SIZE(samples) && flush() != 0) {
		return -EIO;
	}
	if (!time_service_get_ms(&time_ms) || sensor_sample_fetch(die_temp) != 0 ||
	    sensor_channel_get(die_temp, SENSOR_CHAN_DIE_TEMP, &value) != 0) {
		return -EIO;
	}
	if (sample_count == 0) {
		batch_time_ms = time_ms;
	}
	samples[sample_count++].value = (int32_t)(sensor_value_to_micro(&value) / 1000);
	LOG_INF("MCU temperature: %d (C x1000), batch: %u/%u",
		samples[sample_count - 1].value, (unsigned int)sample_count,
		(unsigned int)ARRAY_SIZE(samples));
	return sample_count == ARRAY_SIZE(samples) ? flush() : 0;
}

static void work_handler(struct k_work *work)
{
	if (collect_data() != 0) {
		LOG_WRN("MCU temperature collection failed");
	}
	k_work_schedule(k_work_delayable_from_work(work), K_SECONDS(interval()));
}

static K_WORK_DELAYABLE_DEFINE(work, work_handler);

void telemetry_temperature_register(void)
{
	if (prepare() != 0) {
		LOG_WRN("MCU temperature preparation failed");
		return;
	}
	k_work_schedule(&work, K_NO_WAIT);
}
