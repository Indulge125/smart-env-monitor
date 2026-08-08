#!/usr/bin/env bash
# 固件编译门禁：arm-none-eabi-gcc -fsyntax-only 检查 freertos.c / main.c。
# Keil AC5 无 headless CI，编译级门禁保底线：改坏了代码在 CI 就红。
# 本机用法：CC=path/to/arm-none-eabi-gcc bash ci/compile-gate.sh
set -euo pipefail

FW="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-arm-none-eabi-gcc}"

if ! command -v "$CC" >/dev/null 2>&1; then
  echo "error: $CC not found (set CC=path/to/arm-none-eabi-gcc)" >&2
  exit 2
fi

COMMON=(-fsyntax-only -Wall -Wextra -std=c99 -DUSE_HAL_DRIVER -DSTM32F103xB)
INCS=(
  -I "$FW/Core/Inc"
  -I "$FW/User/Inc"
  -I "$FW/Middlewares/Third_Party/FreeRTOS/Source/include"
  -I "$FW/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2"
  -I "$FW/ci"   # GCC portmacro.h 语法检查夹具（非烧录件，见该文件头注释）
  -I "$FW/Drivers/STM32F1xx_HAL_Driver/Inc"
  -I "$FW/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy"
  -I "$FW/Drivers/CMSIS/Device/ST/STM32F1xx/Include"
  -I "$FW/Drivers/CMSIS/Include"
)

status=0
out="$("$CC" "${COMMON[@]}" "${INCS[@]}" \
       "$FW/Core/Src/freertos.c" "$FW/Core/Src/main.c" 2>&1)" || status=$?
echo "$out"

if [ "$status" -ne 0 ] || echo "$out" | grep -q "error:"; then
  echo "firmware compile gate FAILED" >&2
  exit 1
fi
# 告警（如 CubeMX 任务签名固有的 unused-parameter）不阻断，但打出来让人看见
echo "firmware compile gate OK (warnings, if any, shown above)"
