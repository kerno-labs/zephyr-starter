/* 应用入口：按依赖顺序启动独立子系统。 */
#include "mqtt_uploader.h"
#include "wifi.h"
#include "telemetry/register.h"

int main(void)
{
	/* 先初始化 MQTT，再允许 Wi-Fi 上报已获得地址。 */
	mqtt_uploader_init();
	wifi_start();
	telemetry_temperature_register();
	telemetry_imu_register();
	return 0;
}
