/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file arduino_stubs.cpp
 * @brief The mock Arduino runtime described in arduino_stubs/Arduino.h.
 */

#include "Arduino.h"

#include <string.h>

namespace
{

/** @brief The one clock. */
uint32_t g_micros;
/** @brief Level of each pin, as digitalRead() reports it. */
uint8_t g_level[MOCK_PIN_COUNT];
/** @brief Last value written to each pin. */
int g_written[MOCK_PIN_COUNT];
/** @brief Writes per pin since mock_reset(). */
uint32_t g_writes[MOCK_PIN_COUNT];
/** @brief Interrupt handler per pin, or nullptr. */
void (*g_isr[MOCK_PIN_COUNT])(void);
/** @brief Non-zero while noInterrupts() is in force. */
int g_masked;

/**
 * @brief Set a pin's level and deliver the edge to its handler.
 * @param pin Pin.
 * @param level HIGH or LOW.
 */
void edge(uint8_t pin, uint8_t level)
{
    if (pin >= MOCK_PIN_COUNT)
        return;
    if (g_level[pin] == level)
        return;
    g_level[pin] = level;
    /* An interrupt raised while they are masked is delivered when they are
     * enabled again on real hardware. Here it is dropped, which is the harsher
     * of the two: a test that manages to lose a pulse that way fails rather
     * than passing by luck. */
    if (!g_masked && g_isr[pin])
        g_isr[pin]();
}

} // namespace

uint32_t micros(void)
{
    return g_micros;
}

uint32_t millis(void)
{
    return g_micros / 1000u;
}

void pinMode(uint8_t pin, uint8_t mode)
{
    (void)pin;
    (void)mode;
}

int digitalRead(uint8_t pin)
{
    return pin < MOCK_PIN_COUNT ? g_level[pin] : LOW;
}

void digitalWrite(uint8_t pin, uint8_t value)
{
    analogWrite(pin, value ? 255 : 0);
}

void analogWrite(uint8_t pin, int value)
{
    if (pin >= MOCK_PIN_COUNT)
        return;
    g_written[pin] = value;
    g_writes[pin]++;
}

int digitalPinToInterrupt(uint8_t pin)
{
    return pin < MOCK_PIN_COUNT ? (int)pin : -1;
}

void attachInterrupt(uint8_t irq, void (*fn)(void), int mode)
{
    (void)mode;
    if (irq < MOCK_PIN_COUNT)
        g_isr[irq] = fn;
}

void detachInterrupt(uint8_t irq)
{
    if (irq < MOCK_PIN_COUNT)
        g_isr[irq] = nullptr;
}

void noInterrupts(void)
{
    g_masked++;
}

void interrupts(void)
{
    if (g_masked > 0)
        g_masked--;
}

void mock_reset(void)
{
    g_micros = 0;
    g_masked = 0;
    memset(g_level, 0, sizeof(g_level));
    memset(g_written, 0, sizeof(g_written));
    memset(g_writes, 0, sizeof(g_writes));
    /* A loop rather than memset: an array of function pointers is not
     * guaranteed to be all-bits-zero when null, and clang-tidy is right to say
     * so even though every target here would have been fine. */
    for (size_t i = 0; i < MOCK_PIN_COUNT; i++)
        g_isr[i] = nullptr;
}

void mock_set_micros(uint32_t us)
{
    g_micros = us;
}

void mock_advance_us(uint32_t us)
{
    g_micros += us;
}

void mock_advance_ms(uint32_t ms)
{
    g_micros += ms * 1000u;
}

void mock_pulse(uint8_t pin, uint32_t width_us)
{
    edge(pin, HIGH);
    g_micros += width_us;
    edge(pin, LOW);
}

int mock_analog(uint8_t pin)
{
    return pin < MOCK_PIN_COUNT ? g_written[pin] : 0;
}

uint32_t mock_writes(uint8_t pin)
{
    return pin < MOCK_PIN_COUNT ? g_writes[pin] : 0;
}
