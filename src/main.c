/* 应用入口：按依赖顺序启动独立子系统。 */
#include "mqtt_uploader.h"
#include "network.h"
#include "telemetry/collector.h"

int main(void)
{
	/* 先初始化 MQTT，再允许 Wi-Fi 上报已获得地址。 */
	mqtt_uploader_start();
	network_start();
	telemetry_collector_start();
	return 0;
}
