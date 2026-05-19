#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEST_WORKSPACE_DIR="$(cd "${SCRIPT_DIR}/../../" && pwd)"

source $ZEPHYR_BASE_DIR/.venv/bin/activate
source $ZEPHYR_BASE_DIR/zephyr/zephyr-env.sh
