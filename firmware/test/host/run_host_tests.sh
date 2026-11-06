#!/usr/bin/env bash
# Rev 7.0/8.0/57.0 host-side verification of the encoder mux stack, ESP32-S3 pin map
# and (Rev 8.0) the anti-cogging LUT / electrical-angle / feed-forward math.
#  1. Builds the real firmware sources (tca9548a.cpp, as5600_mux.cpp) against
#     Arduino/Wire/SimpleFOC mocks with -Wall -Wextra -Werror and runs the tests.
#  2. Negative compile test: proves gimbal_pins.h rejects the Rev 6.0 GPIO22 SCL
#     and a duplicated pin at compile time.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../src"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -O1 -Wall -Wextra -Werror -I"$HERE/mock" -I"$SRC")

echo "== build & run unit tests"
"$CXX" "${FLAGS[@]}" -fsanitize=address,undefined \
    "$HERE/test_encoder_mux.cpp" "$SRC/tca9548a.cpp" "$SRC/as5600_mux.cpp" -o "$OUT/t"
"$OUT/t"

echo "== build & run anti-cogging tests (Rev 8.0)"
# The module itself must be free of implicit float->double promotion: the
# ESP32-S3 FPU is single-precision only, doubles in the FOC path are soft-float.
"$CXX" "${FLAGS[@]}" -Wconversion -Wdouble-promotion -fsanitize=address,undefined \
    -c "$SRC/anti_cogging.cpp" -o "$OUT/anti_cogging.o"
"$CXX" "${FLAGS[@]}" -fsanitize=address,undefined "$HERE/test_anti_cogging.cpp" "$OUT/anti_cogging.o" -o "$OUT/ac"
"$OUT/ac"

echo "== build & run IMU tests (Rev 57.0: ICM-42688-P FIFO, Madgwick AHRS, gyro bias(T))"
# Same single-precision gate as anti-cogging: these run at 1 kHz x 2 IMUs on core 0.
IMU_OBJS=()
for m in icm42688 madgwick_ahrs imu_bias imu_pipeline; do
    "$CXX" "${FLAGS[@]}" -Wconversion -Wdouble-promotion -Wshadow -fsanitize=address,undefined \
        -c "$SRC/$m.cpp" -o "$OUT/$m.o"
    IMU_OBJS+=("$OUT/$m.o")
done
"$CXX" "${FLAGS[@]}" -O2 -fsanitize=address,undefined "$HERE/test_imu.cpp" "${IMU_OBJS[@]}" -o "$OUT/imu"
"$OUT/imu"

echo "== negative pin-map compile tests"
neg() {  # $1 description, $2 sed expression applied to a copy of gimbal_pins.h
    mkdir -p "$OUT/neg"
    sed "$2" "$SRC/gimbal_pins.h" > "$OUT/neg/gimbal_pins.h"
    if echo '#include "gimbal_pins.h"' | "$CXX" -std=c++17 -fsyntax-only -I"$OUT/neg" -x c++ - 2>"$OUT/neg/err"; then
        echo "  FAIL: $1 was accepted"; exit 1
    fi
    grep -q "static assertion failed\|static_assert" "$OUT/neg/err" \
        || { echo "  FAIL: $1 rejected for the wrong reason:"; cat "$OUT/neg/err"; exit 1; }
    echo "  PASS: $1 rejected at compile time"
}
neg "Rev 6.0 SCL on non-existent GPIO22" 's/ENC_I2C_SCL   = 18;/ENC_I2C_SCL   = 22;/'
neg "octal-PSRAM GPIO35"                 's/IMU_CS_HANDLE  = 16;/IMU_CS_HANDLE  = 35;/'
neg "strapping GPIO46"                   's/ENC_MUX_RST_N = 40;/ENC_MUX_RST_N = 46;/'
neg "duplicate GPIO17"                   's/ENC_I2C_SCL   = 18;/ENC_I2C_SCL   = 17;/'
neg "VBAT_SENSE moved off ADC1 (swap with GPIO21)" 's/VBAT_SENSE = 1;/VBAT_SENSE = 21;/;s/IMU_SPI_SCK    = 21;/IMU_SPI_SCK    = 1;/'

echo "== build & run motor calibration tests"
g++ -std=c++17 -O1 -I firmware/src -I firmware/test/host/mock firmware/test/host/test_motor_cal.cpp firmware/src/motor_cal.cpp -o /tmp/test_motor_cal && /tmp/test_motor_cal

echo "== build & run joystick tests"
g++ -std=c++17 -O1 -I firmware/src -I firmware/test/host/mock firmware/test/host/test_joystick.cpp firmware/src/joystick_shaper.cpp -o /tmp/test_joystick && /tmp/test_joystick

echo "ALL HOST TESTS PASSED"
