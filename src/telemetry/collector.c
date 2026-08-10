/* 定时采样、双缓冲组批、protobuf 编码与发送队列交接。 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "mqtt_uploader.h"
#include "telemetry.h"
#include "telemetry/collector.h"
#include "telemetry/source.h"
#include "time_service.h"

LOG_MODULE_REGISTER(telemetry_collector, LOG_LEVEL_INF);

struct temperature_batch {
	uint64_t time_ms;
	int32_t values[TELEMETRY_TEMPERATURE_SAMPLES];
	size_t count;
};

struct imu_batch {
	uint64_t time_ms;
	struct telemetry_imu values[TELEMETRY_IMU_SAMPLES];
	size_t count;
};

static struct temperature_batch temperature_batches[2];
static struct imu_batch imu_batches[2];
static uint8_t temperature_active;
static uint8_t imu_active;
static atomic_t temperature_ready = ATOMIC_INIT(0);
static atomic_t imu_ready = ATOMIC_INIT(0);

static void temperature_upload(struct k_work *work);
static void imu_upload(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(temperature_upload_work, temperature_upload);
static K_WORK_DELAYABLE_DEFINE(imu_upload_work, imu_upload);

static void temperature_sample(struct k_work *work)
{
	/* 读取一条温度；只有 RTC 时间有效的样本才能进入批次。 */
	struct temperature_batch *batch = &temperature_batches[temperature_active];
	union telemetry_source_data data;
	uint64_t time_ms;

	if (!time_service_get_ms(&time_ms) || telemetry_temperature_source.get_data(&data) != 0) {
		LOG_WRN("CPU temperature sample skipped: RTC or sensor unavailable");
		goto schedule;
	}
	if (batch->count == 0) {
		batch->time_ms = time_ms;
	}
	batch->values[batch->count++] = data.temperature.value;
	LOG_INF("采集 MCU 温度: %d (°C×1000), 批次: %u/%u",
		data.temperature.value, (unsigned int)batch->count,
		(unsigned int)TELEMETRY_TEMPERATURE_SAMPLES);
	if (batch->count == TELEMETRY_TEMPERATURE_SAMPLES) {
		atomic_set(&temperature_ready, temperature_active + 1);
		temperature_active ^= 1U;
		temperature_batches[temperature_active].count = 0;
		k_work_schedule(&temperature_upload_work, K_NO_WAIT);
	}
schedule:
	k_work_schedule(k_work_delayable_from_work(work), K_SECONDS(telemetry_temperature_source.interval()));
}

static void imu_sample(struct k_work *work)
{
	/* 读取一条六轴数据；批次时间戳取首个样本。 */
	struct imu_batch *batch = &imu_batches[imu_active];
	union telemetry_source_data data;
	uint64_t time_ms;

	if (!time_service_get_ms(&time_ms) || telemetry_imu_source.get_data(&data) != 0) {
		LOG_WRN("IMU sample skipped: RTC or sensor unavailable");
		goto schedule;
	}
	if (batch->count == 0) {
		batch->time_ms = time_ms;
	}
	batch->values[batch->count++] = data.imu;
	LOG_INF("采集 IMU: acc=(%d,%d,%d) m/s²×1000, gyro=(%d,%d,%d) rad/s×1000, 批次: %u/%u",
		data.imu.accel_x, data.imu.accel_y, data.imu.accel_z,
		data.imu.gyro_x, data.imu.gyro_y, data.imu.gyro_z,
		(unsigned int)batch->count, (unsigned int)TELEMETRY_IMU_SAMPLES);
	if (batch->count == TELEMETRY_IMU_SAMPLES) {
		atomic_set(&imu_ready, imu_active + 1);
		imu_active ^= 1U;
		imu_batches[imu_active].count = 0;
		k_work_schedule(&imu_upload_work, K_NO_WAIT);
	}
schedule:
	k_work_schedule(k_work_delayable_from_work(work), K_SECONDS(telemetry_imu_source.interval()));
}

static K_WORK_DELAYABLE_DEFINE(temperature_sample_work, temperature_sample);
static K_WORK_DELAYABLE_DEFINE(imu_sample_work, imu_sample);

static void temperature_upload(struct k_work *work)
{
	/* 编码已完成的非活动缓冲区；网络操作仍只由 mqtt_thread 执行。 */
	uint8_t ready = atomic_get(&temperature_ready);
	uint8_t buffer[CONFIG_APP_MQTT_BUFFER_SIZE];
	size_t length;
	struct temperature_batch *batch;

	ARG_UNUSED(work);
	if (ready == 0) {
		return;
	}
	atomic_set(&temperature_ready, 0);
	batch = &temperature_batches[ready - 1];
	if (telemetry_encode_temperature(batch->time_ms, batch->values, batch->count,
					 buffer, sizeof(buffer), &length) == 0) {
		(void)mqtt_uploader_enqueue(buffer, length);
	}
}

static void imu_upload(struct k_work *work)
{
	/* 编码一个完整 IMU 批次，并将二进制 payload 入队。 */
	uint8_t ready = atomic_get(&imu_ready);
	uint8_t buffer[CONFIG_APP_MQTT_BUFFER_SIZE];
	size_t length;
	struct imu_batch *batch;

	ARG_UNUSED(work);
	if (ready == 0) {
		return;
	}
	atomic_set(&imu_ready, 0);
	batch = &imu_batches[ready - 1];
	if (telemetry_encode_imu(batch->time_ms, batch->values, batch->count,
				  buffer, sizeof(buffer), &length) == 0) {
		(void)mqtt_uploader_enqueue(buffer, length);
	}
}

void telemetry_collector_start(void)
{
	/* 立即安排两个数据源；各自的处理函数随后自行重新调度。 */
	k_work_schedule(&temperature_sample_work, K_NO_WAIT);
	k_work_schedule(&imu_sample_work, K_NO_WAIT);
}
