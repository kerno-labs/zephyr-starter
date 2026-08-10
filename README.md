# Zephyr Starter

CoreS3 的温度与 BMI270 IMU Raw Protobuf 遥测原型。

## 目录

```text
.
├── CMakeLists.txt
├── Kconfig                     # 非敏感运行配置
├── app.local.conf.example       # 本地密钥配置模板
├── proto/telemetry.proto        # starter.iot.telemetry.v1
├── boards/m5stack_cores3_procpu.overlay
└── src/
```

## 构建

在已初始化的 Zephyr workspace 中执行：

```sh
cp app.local.conf.example app.local.conf
west build -p always -b m5stack_cores3/esp32s3/procpu -- -DEXTRA_CONF_FILE=app.local.conf
west flash
```

`app.local.conf` 与 `credentials/` 均不会进入 Git。CA 证书路径相对项目根目录，构建时嵌入固件；CI/量产应生成临时配置和证书文件。ESP32-S3 仅支持 2.4 GHz Wi-Fi。

RTC 使用板载 PCF8563（DTS 节点为 `bm8563_rtc`），不使用 NTP。首次启动经串口执行 `time set <unix_seconds>`；RTC 无效时采集不会入批，MQTTS 也不会建立。

上传 topic 固定为 `/sys/${deviceName}/thing/model/up_raw`，payload 为 protobuf 二进制而非 JSON。CPU 温度每 300 秒采样，默认 6 条组成一批；IMU 每 10 秒采样，默认 60 条组成一批。周期和批大小均可由 Kconfig 覆盖。MQTT 专用线程在 DHCP 得到 IPv4 后才解析 Broker、连接和发送；断线后保留内存中的完整批次，队列满时丢弃最旧批次。
