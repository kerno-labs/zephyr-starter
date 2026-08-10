# Zephyr Starter

最小化 Zephyr 应用起点：启动后输出一条日志，不包含网络、传感器、存储或业务调度代码。

## 目录

```text
.
├── CMakeLists.txt
├── prj.conf
└── src/main.c
```

## 构建

在已初始化的 Zephyr workspace 中执行：

```sh
west build -p always -b m5stack_cores3/esp32s3/procpu .
west flash
```

目标板构建需要已配置 Espressif IDF。仅验证基础构建链路时，可改用本机可用的 Zephyr 测试板，例如 `qemu_riscv64`。
