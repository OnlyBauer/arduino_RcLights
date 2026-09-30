/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file Arduino.h
 * @brief Enough of the Arduino runtime to compile and drive RcLights on a host.
 *
 * **The code under test is the real, shipping `src/RcLights.cpp`** — nothing is
 * reimplemented, and no `#ifdef` was added to it to accommodate this.
 *
 * What carries real behaviour:
 *
 * | Stub | Behaviour |
 * | --- | --- |
 * | `micros()` / `millis()` | one clock the test moves by hand, including across the 32-bit wrap |
 * | `attachInterrupt()` | records the handler; mock_pulse() calls it on both edges, exactly as a receiver would |
 * | `digitalRead()` | returns the level mock_pulse() has set, so the capture sees a real edge sequence |
 * | `analogWrite()` | records the last value per pin, which is what the tests assert on |
 *
 * @par What this deliberately cannot test
 * There is no board. Nothing here says anything about interrupt latency, about
 * whether a given pin really has a timer channel behind it, or about the AVR
 * pin-change path — that one is compiled out on a host, and only a Nano can
 * show whether it works. This exercises the capture, the plumbing into the core
 * and the output writing, and nothing else.
 */

#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#include <stdint.h>
#include <stddef.h>

/** @brief Pin configured as an input. */
#define INPUT 0
/** @brief Pin configured as an output. */
#define OUTPUT 1
/** @brief Pin configured as an input with a pull-up. */
#define INPUT_PULLUP 2

/** @brief Logic low. */
#define LOW 0
/** @brief Logic high. */
#define HIGH 1

/** @brief Interrupt on both edges. */
#define CHANGE 1
/** @brief Interrupt on the rising edge. */
#define RISING 2
/** @brief Interrupt on the falling edge. */
#define FALLING 3

/** @brief Largest pin number the stubs track. */
#define MOCK_PIN_COUNT 64

/** @brief Microseconds since start; the test moves it. @return Microseconds. */
uint32_t micros(void);
/** @brief Milliseconds since start. @return Milliseconds. */
uint32_t millis(void);

/** @brief Configure a pin. @param pin Pin. @param mode One of INPUT/OUTPUT. */
void pinMode(uint8_t pin, uint8_t mode);
/** @brief Read a pin. @param pin Pin. @return HIGH or LOW. */
int digitalRead(uint8_t pin);
/** @brief Drive a pin. @param pin Pin. @param value HIGH or LOW. */
void digitalWrite(uint8_t pin, uint8_t value);
/** @brief Drive a pin with PWM. @param pin Pin. @param value Duty, 0..255. */
void analogWrite(uint8_t pin, int value);

/**
 * @brief Map a pin to its interrupt number.
 * @param pin Pin.
 * @return The same number; every pin is interruptible here.
 */
int digitalPinToInterrupt(uint8_t pin);

/**
 * @brief Install an interrupt handler.
 * @param irq Interrupt number, as returned by digitalPinToInterrupt().
 * @param fn Handler.
 * @param mode Edge selection; only CHANGE is acted on.
 */
void attachInterrupt(uint8_t irq, void (*fn)(void), int mode);

/** @brief Remove an interrupt handler. @param irq Interrupt number. */
void detachInterrupt(uint8_t irq);

/** @brief Block interrupt delivery. */
void noInterrupts(void);
/** @brief Allow interrupt delivery again. */
void interrupts(void);

/* --- the test's side of the mock ------------------------------------------ */

/** @brief Clear every pin, handler and clock back to the start. */
void mock_reset(void);

/** @brief Set the microsecond clock. @param us New value. */
void mock_set_micros(uint32_t us);
/** @brief Advance the microsecond clock. @param us Amount. */
void mock_advance_us(uint32_t us);
/** @brief Advance the clock by whole milliseconds. @param ms Amount. */
void mock_advance_ms(uint32_t ms);

/**
 * @brief Drive one complete servo pulse on a pin, firing both edges.
 *
 * Advances the clock by @p width_us, which is what a real pulse does, so a test
 * that sends a frame on three channels moves time by three pulse widths. That
 * is a few milliseconds and does not disturb anything the core measures.
 *
 * @param pin Pin the receiver channel is wired to.
 * @param width_us Pulse width, in microseconds.
 */
void mock_pulse(uint8_t pin, uint32_t width_us);

/**
 * @brief The last value written to a pin by analogWrite() or digitalWrite().
 * @param pin Pin.
 * @return Duty from 0 to 255; 0 when nothing has been written.
 */
int mock_analog(uint8_t pin);

/**
 * @brief How often a pin has been written since mock_reset().
 * @param pin Pin.
 * @return Write count.
 */
uint32_t mock_writes(uint8_t pin);

#endif /* ARDUINO_MOCK_H */
