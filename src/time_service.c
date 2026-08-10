/* 基于 PCF8563 的 UTC 服务，以及首次串口校时命令。 */
#include "time_service.h"

#include <errno.h>
#include <time.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/timeutil.h>

static const struct device *const rtc = DEVICE_DT_GET(DT_ALIAS(rtc));

bool time_service_get_ms(uint64_t *time_ms)
{
	/* RTC 不可用或未校时时返回 false，避免上传错误时间。 */
	struct rtc_time value;
	int64_t seconds;

	if (!device_is_ready(rtc) || rtc_get_time(rtc, &value) != 0 || value.tm_year < 125) {
		return false;
	}
	seconds = timeutil_timegm64(rtc_time_to_tm(&value));
	if (seconds < 0) {
		return false;
	}
	*time_ms = (uint64_t)seconds * 1000U + value.tm_nsec / 1000000U;
	return true;
}

int time_service_set_unix_seconds(int64_t seconds)
{
	/* 将 UTC Unix 秒转换为 RTC 驱动使用的日历格式。 */
	struct tm tm_value;
	struct rtc_time rtc_value = { 0 };
	time_t unix_time = (time_t)seconds;

	if (!device_is_ready(rtc) || gmtime_r(&unix_time, &tm_value) == NULL) {
		return -ENODEV;
	}
	rtc_value.tm_sec = tm_value.tm_sec;
	rtc_value.tm_min = tm_value.tm_min;
	rtc_value.tm_hour = tm_value.tm_hour;
	rtc_value.tm_mday = tm_value.tm_mday;
	rtc_value.tm_mon = tm_value.tm_mon;
	rtc_value.tm_year = tm_value.tm_year;
	rtc_value.tm_wday = tm_value.tm_wday;
	rtc_value.tm_yday = tm_value.tm_yday;
	rtc_value.tm_isdst = -1;
	return rtc_set_time(rtc, &rtc_value);
}

static int cmd_time_get(const struct shell *shell, size_t argc, char **argv)
{
	/* Shell 适配层：以 Unix 秒显示已校验的 RTC 时间。 */
	uint64_t ms;
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	if (!time_service_get_ms(&ms)) {
		shell_error(shell, "RTC time is invalid; use: time set <unix_seconds>");
		return -EINVAL;
	}
	shell_print(shell, "%llu", ms / 1000U);
	return 0;
}

static int cmd_time_set(const struct shell *shell, size_t argc, char **argv)
{
	/* Shell 适配层：无 NTP 来源时手动设置 UTC。 */
	int err;
	int rtc_err;
	long parsed;
	int64_t seconds;
	parsed = shell_strtol(argc == 2 ? argv[1] : "", 10, &err);
	seconds = parsed;
	if (argc != 2 || err != 0) {
		shell_error(shell, "usage: time set <unix_seconds>");
		return -EINVAL;
	}
	rtc_err = time_service_set_unix_seconds(seconds);
	if (rtc_err != 0) {
		shell_error(shell, "RTC set failed: %d", rtc_err);
		return rtc_err;
	}
	shell_print(shell, "RTC set");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(time_cmds,
	SHELL_CMD(get, NULL, "Show UTC Unix seconds", cmd_time_get),
	SHELL_CMD(set, NULL, "Set UTC: time set <unix_seconds>", cmd_time_set),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(time, &time_cmds, "PCF8563 UTC clock", NULL);
