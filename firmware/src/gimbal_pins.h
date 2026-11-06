/**
 * gimbal_pins.h — ESP32-S3-WROOM-1-N8R8 validated GPIO map (Revision 7.0)
 *
 * Single source of truth for every GPIO used by the gimbal controller.
 * Every assignment is checked at COMPILE TIME against the ESP32-S3 pad set
 * (static_assert below), so an invalid pin (e.g. the GPIO22 used for SCL up to
 * Rev 6.0, which does not exist on the S3) can never be built again.
 *
 * ESP32-S3 pad facts used by the validator (ESP32-S3 TRM / WROOM-1 datasheet):
 *   - Pads that exist:            GPIO0..21, GPIO26..48   (GPIO22..25 do NOT exist)
 *   - In-package SPI flash:       GPIO26..32              (never usable on WROOM-1)
 *   - Octal PSRAM (N8R8 / R8):    GPIO33..37              (unusable on octal-PSRAM modules)
 *   - Strapping pins:             GPIO0, GPIO3, GPIO45, GPIO46 (avoid for outputs)
 *   - Native USB D-/D+:           GPIO19 / GPIO20         (reserved for USB-CDC console/DFU)
 *   - UART0 TXD/RXD:              GPIO43 / GPIO44         (reserved for boot log)
 *   - JTAG MTCK/MTDO/MTDI/MTMS:   GPIO39..42              (reserved for on-board debug)
 *   - ADC1:                       GPIO1..10  (ADC2 is unusable while Wi-Fi is active)
 */
#ifndef GIMBAL_PINS_H
#define GIMBAL_PINS_H

#include <stdint.h>

namespace pins {

// ---------------------------------------------------------------------------
// Motor drivers (3PWM + enable per axis). Pan on MCPWM0, Roll on MCPWM1,
// Pitch falls back to LEDC in SimpleFOC >= 2.3.3 (S3 has only 2 MCPWM units).
// ---------------------------------------------------------------------------
constexpr int8_t PAN_PWM_A   = 4;
constexpr int8_t PAN_PWM_B   = 5;
constexpr int8_t PAN_PWM_C   = 6;
constexpr int8_t PAN_EN      = 7;

constexpr int8_t ROLL_PWM_A  = 8;
constexpr int8_t ROLL_PWM_B  = 9;
constexpr int8_t ROLL_PWM_C  = 10;
constexpr int8_t ROLL_EN     = 11;

constexpr int8_t PITCH_PWM_A = 12;
constexpr int8_t PITCH_PWM_B = 13;
constexpr int8_t PITCH_PWM_C = 14;
constexpr int8_t PITCH_EN    = 15;

// ---------------------------------------------------------------------------
// Encoder I2C bus (I2C0) -> TCA9548A @0x70 -> 3x AS5600 @0x36 (one per channel)
// Rev 6.0 used SDA=21/SCL=22; GPIO22 does not exist on the ESP32-S3.
// ---------------------------------------------------------------------------
constexpr int8_t ENC_I2C_SDA   = 17;
constexpr int8_t ENC_I2C_SCL   = 18;
constexpr int8_t ENC_MUX_RST_N = 2;   // TCA9548A RESET (active low, 10k pull-up to 3V3)

// ---------------------------------------------------------------------------
// Dual ICM-42688-P IMUs on SPI2 (FSPI via GPIO matrix), 24 MHz max SCLK.
// Kept off the encoder I2C bus so IMU traffic never stalls the FOC loop.
// ---------------------------------------------------------------------------
constexpr int8_t IMU_SPI_SCK    = 21;
constexpr int8_t IMU_SPI_MOSI   = 38;
constexpr int8_t IMU_SPI_MISO   = 47;
constexpr int8_t IMU_CS_CAMERA  = 48;
constexpr int8_t IMU_CS_HANDLE  = 16;

// ---------------------------------------------------------------------------
// Analog / housekeeping
// ---------------------------------------------------------------------------
constexpr int8_t VBAT_SENSE = 1;      // ADC1_CH0, 2S pack via 100k/33k divider

// ---------------------------------------------------------------------------
// Compile-time validation
// ---------------------------------------------------------------------------
constexpr bool exists_on_s3(int p)     { return (p >= 0 && p <= 21) || (p >= 26 && p <= 48); }
constexpr bool is_flash_or_psram(int p){ return p >= 26 && p <= 37; }
constexpr bool is_strapping(int p)     { return p == 0 || p == 3 || p == 45 || p == 46; }
constexpr bool is_usb(int p)           { return p == 19 || p == 20; }
constexpr bool is_uart0(int p)         { return p == 43 || p == 44; }
constexpr bool is_jtag(int p)          { return p >= 39 && p <= 42; }
constexpr bool is_adc1(int p)          { return p >= 1 && p <= 10; }

/** A pin is usable for general application I/O on this board. */
constexpr bool usable(int p) {
    return exists_on_s3(p) && !is_flash_or_psram(p) && !is_strapping(p) &&
           !is_usb(p) && !is_uart0(p) && !is_jtag(p);
}

constexpr int8_t ALL[] = {
    PAN_PWM_A, PAN_PWM_B, PAN_PWM_C, PAN_EN,
    ROLL_PWM_A, ROLL_PWM_B, ROLL_PWM_C, ROLL_EN,
    PITCH_PWM_A, PITCH_PWM_B, PITCH_PWM_C, PITCH_EN,
    ENC_I2C_SDA, ENC_I2C_SCL, ENC_MUX_RST_N,
    IMU_SPI_SCK, IMU_SPI_MOSI, IMU_SPI_MISO, IMU_CS_CAMERA, IMU_CS_HANDLE,
    VBAT_SENSE,
};
constexpr unsigned COUNT = sizeof(ALL) / sizeof(ALL[0]);

constexpr bool all_usable(unsigned i = 0) {
    return i >= COUNT ? true : (usable(ALL[i]) && all_usable(i + 1));
}
constexpr bool no_duplicate_from(unsigned i, unsigned j) {
    return j >= COUNT ? true : (ALL[i] != ALL[j] && no_duplicate_from(i, j + 1));
}
constexpr bool all_unique(unsigned i = 0) {
    return i >= COUNT ? true : (no_duplicate_from(i, i + 1) && all_unique(i + 1));
}

static_assert(all_usable(),  "gimbal_pins.h: a GPIO is non-existent, flash/PSRAM, strapping, USB, UART0 or JTAG on ESP32-S3-WROOM-1-N8R8");
static_assert(all_unique(),  "gimbal_pins.h: a GPIO is assigned to more than one function");
static_assert(is_adc1(VBAT_SENSE), "VBAT_SENSE must be on ADC1 (ADC2 is blocked while Wi-Fi is active)");

}  // namespace pins

#endif  // GIMBAL_PINS_H
