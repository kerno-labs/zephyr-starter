/* 为完整温度与 IMU 批次构造 protobuf 信封。 */
#include "telemetry.h"

#include <errno.h>
#include <string.h>
#include <zephyr/random/random.h>
#include <pb_encode.h>
#include "proto/telemetry.pb.h"

static uint32_t boot_id;
static uint32_t sequence;

static uint64_t next_message_id(void)
{
	/* 将每次启动的随机命名空间与单调递增序号组合为消息 ID。 */
	if (boot_id == 0U) {
		boot_id = sys_rand32_get();
		if (boot_id == 0U) {
			boot_id = 1U;
		}
	}
	return ((uint64_t)boot_id << 32) | ++sequence;
}

static int encode(starter_iot_telemetry_v1_PropertyPost *post, uint8_t *out, size_t out_size, size_t *encoded_size)
{
	/* 编码到调用方提供的固定缓冲区；不使用堆内存。 */
	pb_ostream_t stream = pb_ostream_from_buffer(out, out_size);

	if (!pb_encode(&stream, starter_iot_telemetry_v1_PropertyPost_fields, post)) {
		return -EMSGSIZE;
	}
	*encoded_size = stream.bytes_written;
	return 0;
}

int telemetry_encode_temperature(uint64_t time_ms, const int32_t values[], size_t count,
				 uint8_t *out, size_t out_size, size_t *encoded_size)
{
	/* 构造仅包含一个完整温度批次的 Raw PropertyPost。 */
	if (count != TELEMETRY_TEMPERATURE_SAMPLES) {
		return -EINVAL;
	}

	starter_iot_telemetry_v1_PropertyPost post = starter_iot_telemetry_v1_PropertyPost_init_zero;
	post.id = next_message_id();
	strcpy(post.version, "1.0");
	post.sys.ack = false;
	post.method = starter_iot_telemetry_v1_Method_PROPERTY_POST;
	post.params_count = 1;
	post.params[0].time_ms = time_ms;
	post.params[0].which_value = starter_iot_telemetry_v1_Property_cpu_temperature_tag;
	post.params[0].value.cpu_temperature.samples_count = count;
	for (size_t i = 0; i < count; ++i) {
		post.params[0].value.cpu_temperature.samples[i].value = values[i];
	}
	return encode(&post, out, out_size, encoded_size);
}

int telemetry_encode_imu(uint64_t time_ms, const struct telemetry_imu values[], size_t count,
			 uint8_t *out, size_t out_size, size_t *encoded_size)
{
	/* 构造仅包含一个完整 IMU 批次的 Raw PropertyPost。 */
	if (count != TELEMETRY_IMU_SAMPLES) {
		return -EINVAL;
	}

	starter_iot_telemetry_v1_PropertyPost post = starter_iot_telemetry_v1_PropertyPost_init_zero;
	post.id = next_message_id();
	strcpy(post.version, "1.0");
	post.sys.ack = false;
	post.method = starter_iot_telemetry_v1_Method_PROPERTY_POST;
	post.params_count = 1;
	post.params[0].time_ms = time_ms;
	post.params[0].which_value = starter_iot_telemetry_v1_Property_imu_tag;
	post.params[0].value.imu.samples_count = count;
	for (size_t i = 0; i < count; ++i) {
		starter_iot_telemetry_v1_Imu *sample = &post.params[0].value.imu.samples[i];
		sample->accel_x = values[i].accel_x;
		sample->accel_y = values[i].accel_y;
		sample->accel_z = values[i].accel_z;
		sample->gyro_x = values[i].gyro_x;
		sample->gyro_y = values[i].gyro_y;
		sample->gyro_z = values[i].gyro_z;
	}
	return encode(&post, out, out_size, encoded_size);
}
