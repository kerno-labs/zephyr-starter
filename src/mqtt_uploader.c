/* Zephyr MQTT client、MQTTS 生命周期、QoS 1 与重试队列的唯一所有者。 */
#include "mqtt_uploader.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(mqtt_uploader, LOG_LEVEL_INF);

struct outbound_message {
	size_t length;
	uint8_t data[CONFIG_APP_MQTT_BUFFER_SIZE];
};

K_MSGQ_DEFINE(outbound_queue, sizeof(struct outbound_message), CONFIG_APP_MQTT_QUEUE_DEPTH, 4);
K_SEM_DEFINE(network_changed, 0, 1);

static volatile bool network_ready;
static bool connected;
static bool puback_received;
static struct mqtt_client client;
static uint8_t rx_buffer[CONFIG_APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_APP_MQTT_BUFFER_SIZE];
static struct sockaddr_storage broker;
static uint16_t packet_id = 1;
static char topic[128];

#ifdef APP_HAS_CA_CERT
static const uint8_t ca_certificate[] = {
#include "app_ca_cert.inc"
};
#define APP_CA_CERT_TAG 1
static sec_tag_t sec_tags[] = { APP_CA_CERT_TAG };
#endif

static void mqtt_event(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	/* 将 MQTT 库事件转换为 mqtt_thread 使用的少量状态。 */
	ARG_UNUSED(c);
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		connected = evt->result == 0;
		if (!connected) {
			LOG_WRN("MQTT connection refused: %d", evt->result);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		connected = false;
		break;
	case MQTT_EVT_PUBACK:
		if (evt->result == 0) {
			puback_received = true;
		}
		break;
	default:
		break;
	}
}

static int resolve_broker(void)
{
	/* 仅在 DHCP 已使网络可用后，解析配置的 Broker 主机名。 */
	struct zsock_addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
	struct zsock_addrinfo *result;
	char port[6];
	int rc;

	snprintk(port, sizeof(port), "%d", CONFIG_APP_MQTT_PORT);
	rc = zsock_getaddrinfo(CONFIG_APP_MQTT_HOST, port, &hints, &result);
	if (rc != 0) {
		LOG_WRN("Broker DNS resolution failed: %d", rc);
		return -EHOSTUNREACH;
	}
	memcpy(&broker, result->ai_addr, result->ai_addrlen);
	zsock_freeaddrinfo(result);
	return 0;
}

static int connect_mqtt(void)
{
	/* 初始化新的 TLS MQTT client，并发起异步连接。 */
	struct mqtt_sec_config tls = { .peer_verify = 2, .hostname = CONFIG_APP_MQTT_HOST };
	static struct mqtt_utf8 client_id = MQTT_UTF8_LITERAL(CONFIG_APP_DEVICE_NAME);
	static struct mqtt_utf8 username = MQTT_UTF8_LITERAL(CONFIG_APP_MQTT_USERNAME);
	static struct mqtt_utf8 password = MQTT_UTF8_LITERAL(CONFIG_APP_MQTT_PASSWORD);

	if (resolve_broker() != 0) {
		return -EHOSTUNREACH;
	}
#ifndef APP_HAS_CA_CERT
	LOG_ERR("MQTTS CA certificate is not embedded");
	return -EINVAL;
#else
	if (tls_credential_add(APP_CA_CERT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
			       ca_certificate, sizeof(ca_certificate)) != 0) {
		LOG_ERR("MQTTS CA credential registration failed");
		return -EIO;
	}
	tls.sec_tag_list = sec_tags;
	tls.sec_tag_count = 1;
#endif
	mqtt_client_init(&client);
	client.broker = &broker;
	client.evt_cb = mqtt_event;
	client.client_id = client_id;
	client.user_name = &username;
	client.password = &password;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.rx_buf = rx_buffer;
	client.rx_buf_size = sizeof(rx_buffer);
	client.tx_buf = tx_buffer;
	client.tx_buf_size = sizeof(tx_buffer);
	client.transport.type = MQTT_TRANSPORT_SECURE;
	client.transport.tls.config = tls;
	return mqtt_connect(&client);
}

static int publish(const struct outbound_message *message)
{
	/* 发布一条完整 protobuf 批次；仅在收到 PUBACK 后才确认发送成功。 */
	struct mqtt_publish_param param = { 0 };
	struct mqtt_topic mqtt_topic = {
		.topic = { .utf8 = (uint8_t *)topic, .size = strlen(topic) },
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	};

	puback_received = false;
	param.message.topic = mqtt_topic;
	param.message.payload.data = (uint8_t *)message->data;
	param.message.payload.len = message->length;
	param.message_id = packet_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;
	return mqtt_publish(&client, &param);
}

static void mqtt_thread(void)
{
	/* 在独立线程串行执行 DNS、连接、发布、PUBACK 与重连。 */
	struct outbound_message message;
	int backoff_s = 1;

	while (true) {
		k_sem_take(&network_changed, K_FOREVER);
		while (network_ready && IS_ENABLED(CONFIG_APP_MQTT_ENABLED)) {
			if (!connected) {
				if (connect_mqtt() != 0) {
					k_sleep(K_SECONDS(backoff_s));
					backoff_s = MIN(backoff_s * 2, 60);
					continue;
				}
				for (int i = 0; network_ready && i < 20 && !connected; ++i) {
					(void)mqtt_input(&client);
					k_sleep(K_MSEC(100));
				}
				if (!connected) {
					(void)mqtt_disconnect(&client, NULL);
					k_sleep(K_SECONDS(backoff_s));
					backoff_s = MIN(backoff_s * 2, 60);
					continue;
				}
				backoff_s = 1;
			}
			if (k_msgq_get(&outbound_queue, &message, K_SECONDS(1)) != 0) {
				continue;
			}
			if (publish(&message) != 0) {
				connected = false;
				(void)mqtt_uploader_enqueue(message.data, message.length);
				continue;
			}
			for (int i = 0; network_ready && i < 50 && !puback_received; ++i) {
				(void)mqtt_input(&client);
				k_sleep(K_MSEC(100));
			}
			if (!puback_received) {
				connected = false;
				(void)mqtt_uploader_enqueue(message.data, message.length);
			}
		}
		if (connected) {
			(void)mqtt_disconnect(&client, NULL);
			connected = false;
		}
	}
}

K_THREAD_DEFINE(mqtt_thread_id, 4096, mqtt_thread, NULL, NULL, NULL, 5, 0, 0);

void mqtt_uploader_init(void)
{
	/* 启动时一次性生成设备专属的 Raw protobuf topic。 */
	snprintk(topic, sizeof(topic), "/sys/%s/thing/model/up_raw", CONFIG_APP_DEVICE_NAME);
}

void mqtt_uploader_set_network_ready(bool ready)
{
	/* DHCP 就绪或 Wi-Fi 连通性变化时唤醒 MQTT 线程。 */
	network_ready = ready;
	k_sem_give(&network_changed);
}

int mqtt_uploader_enqueue(const uint8_t *data, size_t length)
{
	/* 队列只保存完整批次；满时丢弃最旧的易失批次。 */
	struct outbound_message message;
	struct outbound_message discarded;
	if (length > sizeof(message.data)) {
		return -EMSGSIZE;
	}
	message.length = length;
	memcpy(message.data, data, length);
	if (k_msgq_put(&outbound_queue, &message, K_NO_WAIT) == 0) {
		return 0;
	}
	(void)k_msgq_get(&outbound_queue, &discarded, K_NO_WAIT);
	return k_msgq_put(&outbound_queue, &message, K_NO_WAIT);
}
