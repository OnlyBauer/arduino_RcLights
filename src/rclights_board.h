/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file rclights_board.h
 * @brief Pin assignment, default pin maps per board, and the PWM and interrupt
 *        facts that differ between them.
 *
 * The library is meant to run unchanged on an Arduino Nano, an ST Nucleo and an
 * ESP32 DevKit ("DOIT"). What actually differs between those three is small and
 * is all collected here:
 *
 * | | Nano (AVR) | Nucleo (STM32) | ESP32 DevKit |
 * | --- | --- | --- | --- |
 * | PWM resolution | 8 bit | 8..16 bit, settable | 8..14 bit via LEDC |
 * | `analogWrite()` on any pin | no, six fixed pins | most pins | yes (core 3.x) |
 * | `attachInterrupt()` on any pin | **no**, only D2/D3 | yes | yes |
 * | PWM frequency settable | no | yes | yes |
 *
 * The interrupt row is the one that decides the design. Three receiver channels
 * need three edge-triggered inputs, and an ATmega328P has two external interrupt
 * pins. RcLights therefore brings its own pin-change interrupt capture for AVR
 * (see `RcLights.cpp`), which is why the Nano map below may use pins other than
 * D2 and D3 for the inputs.
 *
 * @par Choosing your own pins
 * The defaults are a starting point, not a requirement. Fill an
 * ::RcLightsPins yourself and pass it to `RcLights::begin()`; anything set to
 * #RCLIGHTS_PIN_NONE is simply not driven, so a car without a reversing light
 * or without an auxiliary output needs no other change.
 */

#ifndef RCLIGHTS_BOARD_H
#define RCLIGHTS_BOARD_H

#include <stdint.h>

/** @brief An output or input that is not connected. */
#define RCLIGHTS_PIN_NONE (-1)

/**
 * @brief Which pin carries which signal.
 *
 * Every field is an Arduino pin number, or #RCLIGHTS_PIN_NONE. The three inputs
 * are the receiver channels; the six outputs drive the LEDs, through a resistor
 * or a transistor as the current demands.
 */
struct RcLightsPins {
    int16_t ch1;       /**< Receiver channel 1, steering. */
    int16_t ch2;       /**< Receiver channel 2, throttle. */
    int16_t ch3;       /**< Receiver channel 3, switch. May be #RCLIGHTS_PIN_NONE. */
    int16_t front;     /**< Park / driving light. */
    int16_t rear;      /**< Tail / brake light. */
    int16_t reverse;   /**< Reversing light. */
    int16_t turnLeft;  /**< Left turn signal. */
    int16_t turnRight; /**< Right turn signal. */
    int16_t aux;       /**< Auxiliary output. */
};

/*
 * The default pin maps.
 *
 * Two rules decided every one of these: an input pin must be able to raise an
 * interrupt, and an output pin must be able to produce PWM. Where a board has
 * more candidates than needed, the ones that clash with something commonly
 * wired (the on-board LED, the boot strapping pins, a debug UART) were left
 * alone.
 *
 * Every pin is a separate macro, and every one is guarded, so a single pin can
 * be moved from the build without touching this file or the sketch:
 *
 *   platformio.ini   build_flags = -DRCLIGHTS_PIN_FRONT=6
 *   arduino-cli      --build-property "compiler.cpp.extra_flags=-DRCLIGHTS_PIN_FRONT=6"
 *
 * The Arduino IDE has no field for build flags. There, define the macro in the
 * sketch before including RcLights.h and pass RCLIGHTS_PINS_DEFAULT to
 * begin() -- which is what the RcLightsCar example does, so the override works
 * either way round.
 */

#if defined(ARDUINO_ARCH_AVR)

/* Nano / Uno / Pro Mini. The six hardware PWM pins are D3, D5, D6, D9, D10 and
 * D11, and all six are outputs here -- which is exactly as many as the library
 * has outputs, so the inputs have to go elsewhere. D4, D7 and D8 are ordinary
 * pins on PORTD/PORTB and are served by the pin-change capture.
 *
 * Note that D5 and D6 sit on timer0, the same timer millis() uses: their PWM
 * runs at 976 Hz instead of 490 Hz and cannot be changed. Both are LED outputs
 * here, where neither matters.
 *
 * Moving an output to a pin that is not one of those six is the one change
 * that fails quietly: analogWrite() on any other AVR pin is on-or-off, so the
 * light works but never dims. */

/** @brief Name of the board family this build targets. */
#define RCLIGHTS_BOARD_NAME "AVR"

#ifndef RCLIGHTS_PIN_STEERING
/** @brief Receiver channel 1, steering. */
#define RCLIGHTS_PIN_STEERING 4
#endif
#ifndef RCLIGHTS_PIN_THROTTLE
/** @brief Receiver channel 2, throttle. */
#define RCLIGHTS_PIN_THROTTLE 7
#endif
#ifndef RCLIGHTS_PIN_CH3
/** @brief Receiver channel 3, the switch. */
#define RCLIGHTS_PIN_CH3 8
#endif
#ifndef RCLIGHTS_PIN_FRONT
/** @brief Park light / driving light. */
#define RCLIGHTS_PIN_FRONT 3
#endif
#ifndef RCLIGHTS_PIN_REAR
/** @brief Tail light / brake light. */
#define RCLIGHTS_PIN_REAR 5
#endif
#ifndef RCLIGHTS_PIN_REVERSE
/** @brief Reversing light. */
#define RCLIGHTS_PIN_REVERSE 6
#endif
#ifndef RCLIGHTS_PIN_SIGNAL_LEFT
/** @brief Left turn signal. */
#define RCLIGHTS_PIN_SIGNAL_LEFT 9
#endif
#ifndef RCLIGHTS_PIN_SIGNAL_RIGHT
/** @brief Right turn signal. */
#define RCLIGHTS_PIN_SIGNAL_RIGHT 10
#endif
#ifndef RCLIGHTS_PIN_AUX
/** @brief Auxiliary output. */
#define RCLIGHTS_PIN_AUX 11
#endif

#elif defined(ARDUINO_ARCH_ESP32)

/* ESP32 DevKit v1 ("DOIT", 30 pins). GPIO 34/35/36/39 are input-only and have
 * no pull-ups, which is right for a receiver output driving them: nothing on
 * the board can fight the signal. GPIO 32 completes the three.
 *
 * The outputs avoid the strapping pins (0, 2, 5, 12, 15), the flash pins
 * (6..11, which are not brought out and would brick a running sketch) and the
 * input-only range. */

#define RCLIGHTS_BOARD_NAME "ESP32"

#ifndef RCLIGHTS_PIN_STEERING
#define RCLIGHTS_PIN_STEERING 34
#endif
#ifndef RCLIGHTS_PIN_THROTTLE
#define RCLIGHTS_PIN_THROTTLE 35
#endif
#ifndef RCLIGHTS_PIN_CH3
#define RCLIGHTS_PIN_CH3 32
#endif
#ifndef RCLIGHTS_PIN_FRONT
#define RCLIGHTS_PIN_FRONT 13
#endif
#ifndef RCLIGHTS_PIN_REAR
#define RCLIGHTS_PIN_REAR 14
#endif
#ifndef RCLIGHTS_PIN_REVERSE
#define RCLIGHTS_PIN_REVERSE 27
#endif
#ifndef RCLIGHTS_PIN_SIGNAL_LEFT
#define RCLIGHTS_PIN_SIGNAL_LEFT 26
#endif
#ifndef RCLIGHTS_PIN_SIGNAL_RIGHT
#define RCLIGHTS_PIN_SIGNAL_RIGHT 25
#endif
#ifndef RCLIGHTS_PIN_AUX
#define RCLIGHTS_PIN_AUX 33
#endif

#elif defined(ARDUINO_ARCH_STM32)

/* Nucleo-64, Arduino header numbering, so this holds for an F103, an F411 or an
 * L476 alike. Every pin on that header can raise an interrupt and D3, D5, D6,
 * D9, D10 and D11 carry a timer channel on all of them.
 *
 * D13 is deliberately unused: it is the user LED and, on several Nucleos, also
 * SCK. */

#define RCLIGHTS_BOARD_NAME "STM32"

#ifndef RCLIGHTS_PIN_STEERING
#define RCLIGHTS_PIN_STEERING 2
#endif
#ifndef RCLIGHTS_PIN_THROTTLE
#define RCLIGHTS_PIN_THROTTLE 4
#endif
#ifndef RCLIGHTS_PIN_CH3
#define RCLIGHTS_PIN_CH3 7
#endif
#ifndef RCLIGHTS_PIN_FRONT
#define RCLIGHTS_PIN_FRONT 3
#endif
#ifndef RCLIGHTS_PIN_REAR
#define RCLIGHTS_PIN_REAR 5
#endif
#ifndef RCLIGHTS_PIN_REVERSE
#define RCLIGHTS_PIN_REVERSE 6
#endif
#ifndef RCLIGHTS_PIN_SIGNAL_LEFT
#define RCLIGHTS_PIN_SIGNAL_LEFT 9
#endif
#ifndef RCLIGHTS_PIN_SIGNAL_RIGHT
#define RCLIGHTS_PIN_SIGNAL_RIGHT 10
#endif
#ifndef RCLIGHTS_PIN_AUX
#define RCLIGHTS_PIN_AUX 11
#endif

#else

/* Unknown core: the Arduino-standard numbering, which is right often enough to
 * compile and be a sensible starting point. Check it against your board's pin
 * table before wiring anything. */

#define RCLIGHTS_BOARD_NAME "generic"

#ifndef RCLIGHTS_PIN_STEERING
#define RCLIGHTS_PIN_STEERING 2
#endif
#ifndef RCLIGHTS_PIN_THROTTLE
#define RCLIGHTS_PIN_THROTTLE 4
#endif
#ifndef RCLIGHTS_PIN_CH3
#define RCLIGHTS_PIN_CH3 7
#endif
#ifndef RCLIGHTS_PIN_FRONT
#define RCLIGHTS_PIN_FRONT 3
#endif
#ifndef RCLIGHTS_PIN_REAR
#define RCLIGHTS_PIN_REAR 5
#endif
#ifndef RCLIGHTS_PIN_REVERSE
#define RCLIGHTS_PIN_REVERSE 6
#endif
#ifndef RCLIGHTS_PIN_SIGNAL_LEFT
#define RCLIGHTS_PIN_SIGNAL_LEFT 9
#endif
#ifndef RCLIGHTS_PIN_SIGNAL_RIGHT
#define RCLIGHTS_PIN_SIGNAL_RIGHT 10
#endif
#ifndef RCLIGHTS_PIN_AUX
#define RCLIGHTS_PIN_AUX 11
#endif

#endif

/**
 * @brief This board's pin map, in ::RcLightsPins field order.
 *
 * Pass it to `RcLights::begin()`. Any pin that a build flag or a `#define` in
 * the sketch has already set keeps that value.
 */
#define RCLIGHTS_PINS_DEFAULT {RCLIGHTS_PIN_STEERING, RCLIGHTS_PIN_THROTTLE, RCLIGHTS_PIN_CH3, RCLIGHTS_PIN_FRONT, RCLIGHTS_PIN_REAR, RCLIGHTS_PIN_REVERSE, RCLIGHTS_PIN_SIGNAL_LEFT, RCLIGHTS_PIN_SIGNAL_RIGHT, RCLIGHTS_PIN_AUX}

/*
 * PWM back end.
 *
 * ESP32 before Arduino core 3.0 has no analogWrite() and drives LEDs through
 * the LEDC peripheral, one channel per output. From core 3.0 analogWrite()
 * exists and maps onto LEDC itself, so the special case is only for the older
 * core — which is still what a great many installed toolchains have.
 *
 * Note that CI installs the current esp32 core, so it compiles the
 * analogWrite() branch and not this one. The LEDC branch is the least-covered
 * code in the library: nothing but a 2.x toolchain will tell you it still
 * builds.
 */
#if defined(ARDUINO_ARCH_ESP32)
#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#define RCLIGHTS_PWM_LEDC 1
#endif
#endif

#ifndef RCLIGHTS_PWM_LEDC
/** @brief Non-zero when the ESP32 LEDC peripheral is driven directly. */
#define RCLIGHTS_PWM_LEDC 0
#endif

/**
 * @brief Default PWM carrier frequency, in Hz, where the board can set one.
 *
 * Above the flicker fusion threshold by a wide margin, so a camera pointed at
 * the car does not see banding, and low enough that a MOSFET driving a string
 * of LEDs still switches cleanly.
 */
#ifndef RCLIGHTS_PWM_HZ
#define RCLIGHTS_PWM_HZ 1000u
#endif

/**
 * @brief Attribute for an interrupt handler that must not live in flash.
 *
 * On ESP32 an ISR that runs while the flash cache is disabled — during an SPI
 * flash write, for instance — must be in IRAM or the chip panics. Everywhere
 * else this expands to nothing.
 */
#if defined(ARDUINO_ARCH_ESP32)
#define RCLIGHTS_ISR_ATTR IRAM_ATTR
#else
#define RCLIGHTS_ISR_ATTR
#endif

#endif /* RCLIGHTS_BOARD_H */
