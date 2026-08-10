#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 读取已校时的 PCF8563 UTC 时间，返回 Unix 毫秒。 */
bool time_service_get_ms(uint64_t *time_ms);
/* 使用 Unix 秒设置 PCF8563 UTC 时间。 */
int time_service_set_unix_seconds(int64_t seconds);
