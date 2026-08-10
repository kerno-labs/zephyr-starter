#!/usr/bin/env bash
# 构建 CoreS3 遥测固件，并在构建成功后刷写到开发板。

set -euo pipefail

PROJECT_DIR="/Users/hope/code/zhg/aiot/zephyr-starter"
ZEPHYR_WORKSPACE="/Users/hope/zephyrproject"
VENV_DIR="/Users/hope/zephyrproject/.venv"

if [[ ! -f "$VENV_DIR/bin/activate" ]]; then
  echo "未找到 Zephyr Python 虚拟环境: $VENV_DIR" >&2
  exit 1
fi

source "$VENV_DIR/bin/activate"
cd "$ZEPHYR_WORKSPACE"

west build -p always \
  -d "$PROJECT_DIR/build" \
  -b m5stack_cores3/esp32s3/procpu \
  -s "$PROJECT_DIR" \
  -- -DEXTRA_CONF_FILE="$PROJECT_DIR/app.local.conf"

west flash -d "$PROJECT_DIR/build"
