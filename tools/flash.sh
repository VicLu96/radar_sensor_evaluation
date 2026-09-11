#!/usr/bin/env bash
# Flash one of the out-of-tree test builds without setting up the nRF Connect
# terminal environment by hand.
#
#   ./tools/flash.sh beacon      Zephyr's beacon sample  (is the fault in OUR code?)
#   ./tools/flash.sh radio_test  Nordic's radio test     (does RF leave at all?)
#   ./tools/flash.sh app         firmware_test from build_1
#
# These live outside firmware_test/, so the nRF Connect VS Code extension does
# not see them as applications and the Flash button does not apply to them.
set -euo pipefail

TC="C:/ncs/toolchains/936afb6332"
export PATH="$TC/opt/bin:$TC/opt/bin/Scripts:$TC/mingw64/bin:$TC/opt/zephyr-sdk/arm-zephyr-eabi/bin:$PATH"
export ZEPHYR_SDK_INSTALL_DIR="$TC/opt/zephyr-sdk"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "${1:-}" in
  beacon)     DIR="$ROOT/tools/ble_beacon/build" ;;
  radio_test) DIR="$ROOT/tools/radio_test/build" ;;
  app)        DIR="$ROOT/firmware_test/build_1" ;;
  *) echo "usage: $0 {beacon|radio_test|app}" >&2; exit 2 ;;
esac

[ -f "$DIR/merged.hex" ] || { echo "no build at $DIR - build it first" >&2; exit 1; }

echo "flashing $1 from $DIR"
cd "C:/ncs/v3.3.0"
west flash --build-dir "$DIR"
