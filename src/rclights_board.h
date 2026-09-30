/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file rclights_board.h
 * @brief Default pin maps, and the PWM and interrupt facts that differ between
 *        boards.
 *
 * An ATmega328P can attach an interrupt to two pins and this needs three, which
 * is why AVR gets its own pin-change capture (see RcLights.cpp) and why the Nano
 * map uses pins other than D2 and D3 for the inputs.
 *
 * The defaults are a starting point. Fill an ::RcLightsPins yourself and pass it
 * to `RcLights::begin()`; #RCLIGHTS_PIN_NONE means "not driven".
 */

#ifndef RCLIGHTS_BOARD_H
#define RCLIGHTS_BOARD_H

#include <stdint.h>

/** @brief An output or input that is not connected. */
#define RCLIGHTS_PIN_NONE (-1)

/** @brief Which pin carries which signal, as Arduino pin numbers. */
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
 * The default pin maps. Every input can raise an interrupt, every output has a
 * PWM timer, and pins commonly wired to something else are avoided.
 *
 * Each pin is separately guarded, so one can be moved from the build:
 *
 *   platformio.ini   build_flags = -DRCLIGHTS_PIN_FRONT=6
 *   arduino-cli      --build-property "compiler.cpp.extra_flags=-DRCLIGHTS_PIN_FRONT=6"
 *
 * The Arduino IDE has no such field; there, #define it in the sketch above the
 * include, which works because the sketch passes RCLIGHTS_PINS_DEFAULT to
 * begin().
 */

#if defined(ARDUINO_ARCH_AVR)

/* Nano / Uno / Pro Mini. The six hardware PWM pins (D3, D5, D6, D9, D10, D11)
 * are exactly the six outputs, so the inputs go on pins served by the
 * pin-change capture.
 *
 * Moving an output off those six fails quietly: analogWrite() on any other AVR
 * pin is on-or-off, so the light works but never dims. */

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

/* ESP32 DevKit v1 ("DOIT"). GPIO 34/35 are input-only with no pull-ups, which
 * suits a receiver output. The outputs avoid the strapping pins (0, 2, 5, 12,
 * 15) and the flash pins (6..11). */

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

/* Nucleo-64 in Arduino header numbering, so this holds for an F103, F411 or
 * L476 alike. D13 is left alone: user LED, and SCK on several Nucleos. */

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

/* Unknown core: Arduino-standard numbering. Check it against your board's pin
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

/** @brief This board's pin map, in ::RcLightsPins field order. */
#define RCLIGHTS_PINS_DEFAULT {RCLIGHTS_PIN_STEERING, RCLIGHTS_PIN_THROTTLE, RCLIGHTS_PIN_CH3, RCLIGHTS_PIN_FRONT, RCLIGHTS_PIN_REAR, RCLIGHTS_PIN_REVERSE, RCLIGHTS_PIN_SIGNAL_LEFT, RCLIGHTS_PIN_SIGNAL_RIGHT, RCLIGHTS_PIN_AUX}

/*
 * ESP32 before Arduino core 3.0 has no analogWrite() and needs LEDC directly.
 * CI installs the current core, so that branch is the least-covered code here:
 * only a 2.x toolchain will tell you it still builds.
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
 * High enough that a camera sees no banding, low enough that a MOSFET still
 * switches cleanly.
 */
#ifndef RCLIGHTS_PWM_HZ
#define RCLIGHTS_PWM_HZ 1000u
#endif

/**
 * @brief Attribute for an interrupt handler that must not live in flash.
 *
 * On ESP32 an ISR running while the flash cache is disabled must be in IRAM or
 * the chip panics. Elsewhere this expands to nothing.
 */
#if defined(ARDUINO_ARCH_ESP32)
#define RCLIGHTS_ISR_ATTR IRAM_ATTR
#else
#define RCLIGHTS_ISR_ATTR
#endif

#endif /* RCLIGHTS_BOARD_H */
