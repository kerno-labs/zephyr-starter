/* Wi-Fi 生命周期模块：只上报网络就绪状态，不直接操作 MQTT。 */
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/util.h>

#include "mqtt_uploader.h"
#include "network.h"

LOG_MODULE_REGISTER(network, LOG_LEVEL_INF);

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	/* 仅在 DHCP 完成后，DNS 与 MQTT 才具备运行条件。 */
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);
	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		LOG_INF("Wi-Fi obtained IPv4 address");
		mqtt_uploader_set_network_ready(true);
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("Wi-Fi disconnected");
		mqtt_uploader_set_network_ready(false);
	}
}

static struct net_mgmt_event_callback wifi_events;

void network_start(void)
{
	/* 先注册事件，避免遗漏过早到达的 DHCP 事件。 */
	struct wifi_connect_req_params params = {
		.ssid = CONFIG_APP_WIFI_SSID,
		.ssid_length = strlen(CONFIG_APP_WIFI_SSID),
		.psk = CONFIG_APP_WIFI_PSK,
		.psk_length = strlen(CONFIG_APP_WIFI_PSK),
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
	};

	net_mgmt_init_event_callback(&wifi_events, wifi_event,
		NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_events);

	if (!IS_ENABLED(CONFIG_APP_MQTT_ENABLED)) {
		return;
	}
	if (params.ssid_length == 0 || params.psk_length == 0) {
		LOG_WRN("MQTT enabled but Wi-Fi configuration is missing");
		return;
	}
	if (net_mgmt(NET_REQUEST_WIFI_CONNECT, net_if_get_default(), &params, sizeof(params)) != 0) {
		LOG_WRN("Wi-Fi connection request failed");
	}
}
