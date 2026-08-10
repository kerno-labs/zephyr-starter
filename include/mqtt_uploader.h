#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* 构造设备专属 MQTT topic 并初始化上传模块状态。 */
void mqtt_uploader_start(void);
/* 通知上传模块 DHCP 网络是否已可用。 */
void mqtt_uploader_set_network_ready(bool ready);
/* 将一条完整 protobuf 批次入队；返回 errno 风格结果。 */
int mqtt_uploader_enqueue(const uint8_t *data, size_t length);
