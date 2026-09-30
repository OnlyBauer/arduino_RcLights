/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLights.cpp
 * @brief Pulse capture, PWM output and the board-specific parts of both.
 *
 * Three things happen here and nothing else: edges are timestamped into
 * ::RcLights::Capture from interrupt context, loop() moves those measurements
 * into the core, and the core's six brightness values are written to pins. No
 * decision about the lights is taken in this file.
 */

#include "RcLights.h"

namespace
{

/** @brief Shortest edge-to-edge time still taken for a servo pulse, in µs. */
const uint32_t kPulseFloorUs = 500;
/** @brief Longest edge-to-edge time still taken for a servo pulse, in µs. */
const uint32_t kPulseCeilUs = 2500;

/** @brief Every output's inversion bit set. */
const uint8_t kInvertAll = (uint8_t)((1u << RCL_OUT_COUNT) - 1u);

} // namespace

RcLights *RcLights::s_active = nullptr;

RcLights::RcLights()
    : rcl_config_t(), outputs(RCLIGHTS_OUTPUTS_ACTIVE_HIGH), m_state(), m_pins(),
      m_cap(), m_pulse(), m_invert(0), m_written(), m_pwmHz(RCLIGHTS_PWM_HZ),
      m_running(false)
{
    /* The inherited settings start where rcl_config_default() puts them, so a
     * sketch that writes none of them gets the same car as one that writes them
     * all to their defaults. */
    rcl_config_default(this);

    const RcLightsPins defaults = RCLIGHTS_PINS_DEFAULT;
    m_pins = defaults;
    for (uint8_t i = 0; i < RCL_CH_COUNT; i++) {
        m_cap[i].rise_us = 0;
        m_cap[i].pulse_us = 0;
        m_cap[i].fresh = false;
        m_pulse[i] = 0;
    }
    for (uint8_t i = 0; i < RCL_OUT_COUNT; i++)
        m_written[i] = 0;
}

RcLights::~RcLights()
{
    end();
}

bool RcLights::begin(const RcLightsPins &pins)
{
    /* The settings are the object's own inherited fields. The copy is needed
     * because the overload below assigns to them, and assigning a structure to
     * itself through a reference is not something to rely on. */
    const rcl_config_t cfg = *this;
    return begin(pins, cfg);
}

bool RcLights::begin(const RcLightsPins &pins, const rcl_config_t &cfg)
{
    if (s_active != nullptr && s_active != this)
        return false;

    /* A configured channel 3 with no pin to read it on would leave the core
     * waiting for a channel that can never arrive, and the whole controller
     * would sit in failsafe with the hazards flashing. Refusing here points at
     * the actual mistake. */
    if (cfg.aux_mode != RCL_AUX_MODE_OFF && pins.ch3 == RCLIGHTS_PIN_NONE)
        return false;

    if (!rcl_init(&m_state, &cfg, millis()))
        return false;

    m_pins = pins;

    /* Keep the object's own fields honest: they must report what was applied,
     * not what they happened to hold when a configuration arrived from
     * elsewhere. */
    *static_cast<rcl_config_t *>(this) = cfg;

    /* Before the pins are first driven, below. An active-low string would
     * otherwise sit at full brightness for the length of this function. */
    m_invert = outputs == RCLIGHTS_OUTPUTS_ACTIVE_LOW ? kInvertAll : 0;

    for (uint8_t i = 0; i < RCL_CH_COUNT; i++) {
        m_cap[i].rise_us = 0;
        m_cap[i].pulse_us = 0;
        m_cap[i].fresh = false;
        m_pulse[i] = 0;
    }

#if defined(ARDUINO_ARCH_STM32)
    analogWriteFrequency(m_pwmHz);
#endif

    for (uint8_t i = 0; i < RCL_OUT_COUNT; i++) {
        int16_t pin = outputPin((rcl_output_t)i);
        if (pin == RCLIGHTS_PIN_NONE)
            continue;
        pinMode((uint8_t)pin, OUTPUT);
#if RCLIGHTS_PWM_LEDC
        /* One LEDC channel per output, numbered like the outputs. Eight bits,
         * which is what the core produces; asking for more would only add
         * resolution the brightness values do not carry. */
        ledcSetup(i, m_pwmHz, 8);
        ledcAttachPin((uint8_t)pin, i);
#endif
        m_written[i] = 0;
        writePin((rcl_output_t)i, 0);
    }

    if (!attachInputs()) {
        detachInputs();
        return false;
    }

    s_active = this;
    m_running = true;
    return true;
}

void RcLights::end()
{
    if (!m_running)
        return;

    detachInputs();

    for (uint8_t i = 0; i < RCL_OUT_COUNT; i++)
        writePin((rcl_output_t)i, 0);

    if (s_active == this)
        s_active = nullptr;
    m_running = false;
}

void RcLights::loop()
{
    if (!m_running)
        return;

    rcl_input_t in;
    for (uint8_t i = 0; i < RCL_CH_COUNT; i++) {
        bool fresh = false;
        uint16_t pulse = 0;

        if (inputPin(i) != RCLIGHTS_PIN_NONE) {
            /* The capture fields are written from an interrupt and are wider
             * than one byte on AVR, so they are read with interrupts off.
             * noInterrupts()/interrupts() nest correctly on all three cores and
             * the window here is a few instructions. */
            noInterrupts();
            fresh = m_cap[i].fresh;
            pulse = m_cap[i].pulse_us;
            m_cap[i].fresh = false;
            interrupts();
        }

        if (fresh)
            m_pulse[i] = pulse;
        in.pulse_us[i] = m_pulse[i];
        in.fresh[i] = fresh;
    }

    rcl_update(&m_state, &in, millis());
    writeOutputs();
}

bool RcLights::applyConfig(const rcl_config_t &cfg)
{
    if (!rcl_config_validate(&cfg))
        return false;
    if (cfg.aux_mode != RCL_AUX_MODE_OFF && m_pins.ch3 == RCLIGHTS_PIN_NONE)
        return false;

    m_state.cfg = cfg;
    *static_cast<rcl_config_t *>(this) = cfg;
    return true;
}

bool RcLights::apply()
{
    const rcl_config_t cfg = *this;
    return applyConfig(cfg);
}

const rcl_config_t &RcLights::config() const
{
    return m_state.cfg;
}

const RcLightsPins &RcLights::pins() const
{
    return m_pins;
}

uint16_t RcLights::pulseUs(rcl_channel_t ch) const
{
    if ((int)ch < 0 || ch >= RCL_CH_COUNT)
        return 0;
    return m_pulse[ch];
}

bool RcLights::channelValid(rcl_channel_t ch) const
{
    if ((int)ch < 0 || ch >= RCL_CH_COUNT)
        return false;
    return m_state.chan_valid[ch];
}

int16_t RcLights::channel(rcl_channel_t ch) const
{
    return rcl_channel(&m_state, ch);
}

uint8_t RcLights::output(rcl_output_t out) const
{
    return rcl_output(&m_state, out);
}

rcl_drive_t RcLights::drive() const
{
    return m_state.drive;
}

bool RcLights::failsafe() const
{
    return m_state.failsafe;
}

bool RcLights::centering() const
{
    return rcl_centering(&m_state);
}

const rcl_state_t &RcLights::state() const
{
    return m_state;
}

void RcLights::setInvertedOutputs(uint8_t mask)
{
    m_invert = mask;
    if (!m_running)
        return;
    /* Rewrite every pin at once: half the outputs changing polarity at the next
     * brightness change and the other half staying wrong would be worse than
     * either. */
    for (uint8_t i = 0; i < RCL_OUT_COUNT; i++)
        writePin((rcl_output_t)i, m_written[i]);
}

void RcLights::setPwmFrequency(uint32_t hz)
{
    m_pwmHz = hz;
}

bool RcLights::captureCenter()
{
    rcl_config_t cfg = m_state.cfg;

    /* Steering and throttle only. Channel 3 is a switch: its rest position is
     * whichever way it happens to be flicked, and taking that as a centre would
     * put the centre on top of an endpoint. */
    for (uint8_t i = 0; i < RCL_CH_AUX; i++) {
        if (!m_state.chan_valid[i])
            return false;
        cfg.cal[i].center_us = m_pulse[i];
    }

    /* applyConfig() rather than a direct assignment: it validates -- which
     * catches a centre that has landed outside its own endpoints, what happens
     * when this is called with a stick held over -- and it keeps the object's
     * own fields in step with what is running. */
    return applyConfig(cfg);
}

int16_t RcLights::outputPin(rcl_output_t out) const
{
    switch (out) {
    case RCL_OUT_FRONT:
        return m_pins.front;
    case RCL_OUT_REAR:
        return m_pins.rear;
    case RCL_OUT_REVERSE:
        return m_pins.reverse;
    case RCL_OUT_TURN_LEFT:
        return m_pins.turnLeft;
    case RCL_OUT_TURN_RIGHT:
        return m_pins.turnRight;
    case RCL_OUT_AUX:
        return m_pins.aux;
    default:
        return RCLIGHTS_PIN_NONE;
    }
}

int16_t RcLights::inputPin(uint8_t ch) const
{
    switch (ch) {
    case RCL_CH_STEER:
        return m_pins.ch1;
    case RCL_CH_THROTTLE:
        return m_pins.ch2;
    case RCL_CH_AUX:
        return m_pins.ch3;
    default:
        return RCLIGHTS_PIN_NONE;
    }
}

void RcLights::writePin(rcl_output_t out, uint8_t level)
{
    int16_t pin = outputPin(out);
    if (pin == RCLIGHTS_PIN_NONE)
        return;

    uint8_t v = (m_invert & (uint8_t)(1u << out)) ? (uint8_t)(RCL_LEVEL_MAX - level) : level;

#if RCLIGHTS_PWM_LEDC
    (void)pin;
    ledcWrite((uint8_t)out, v);
#else
    analogWrite((uint8_t)pin, v);
#endif
}

void RcLights::writeOutputs()
{
    for (uint8_t i = 0; i < RCL_OUT_COUNT; i++) {
        uint8_t level = rcl_output(&m_state, (rcl_output_t)i);
        if (level == m_written[i])
            continue;
        m_written[i] = level;
        writePin((rcl_output_t)i, level);
    }
}

void RcLights::onEdgeLevel(uint8_t idx, bool high)
{
    uint32_t now = micros();

    if (high) {
        m_cap[idx].rise_us = now;
        return;
    }

    /* Unsigned subtraction, so the 32-bit micros() wrap every 71 minutes needs
     * no handling: the difference across it is still correct. */
    uint32_t width = now - m_cap[idx].rise_us;
    if (width < kPulseFloorUs || width > kPulseCeilUs)
        return;

    m_cap[idx].pulse_us = (uint16_t)width;
    m_cap[idx].fresh = true;
}

void RcLights::onEdge(uint8_t idx)
{
    int16_t pin = inputPin(idx);
    if (pin == RCLIGHTS_PIN_NONE)
        return;
    onEdgeLevel(idx, digitalRead((uint8_t)pin) == HIGH);
}

void RcLights::isr0()
{
    if (s_active)
        s_active->onEdge(0);
}

void RcLights::isr1()
{
    if (s_active)
        s_active->onEdge(1);
}

void RcLights::isr2()
{
    if (s_active)
        s_active->onEdge(2);
}

/* --- input capture, per architecture ---------------------------------------
 *
 * Everything except AVR can attach an interrupt to any pin, so the generic path
 * is three attachInterrupt() calls. AVR cannot: an ATmega328P has exactly two
 * external interrupt pins, D2 and D3, and three channels do not fit on two
 * pins. The pin-change interrupts do cover every pin, at the cost of not
 * telling you which one changed, so the handler re-reads the channels on the
 * port that fired and compares against the level it saw last.
 */

#if defined(ARDUINO_ARCH_AVR) && !defined(RCLIGHTS_NO_AVR_PCINT)

/** @brief AVR pin-change interrupt for PORTB. */
ISR(PCINT0_vect)
{
    RcLights::avrIsrEntry(0);
}

/** @brief AVR pin-change interrupt for PORTC. */
ISR(PCINT1_vect)
{
    RcLights::avrIsrEntry(1);
}

/** @brief AVR pin-change interrupt for PORTD. */
ISR(PCINT2_vect)
{
    RcLights::avrIsrEntry(2);
}

void RcLights::avrIsrEntry(uint8_t port)
{
    RcLights *self = s_active;
    if (!self)
        return;

    for (uint8_t i = 0; i < RCL_CH_COUNT; i++) {
        int16_t pin = self->inputPin(i);
        if (pin == RCLIGHTS_PIN_NONE || self->m_avrPort[i] != port)
            continue;
        uint8_t level = digitalRead((uint8_t)pin);
        if (level == self->m_avrLevel[i])
            continue;
        self->m_avrLevel[i] = level;
        self->onEdgeLevel(i, level == HIGH);
    }
}

bool RcLights::attachInputs()
{
    for (uint8_t i = 0; i < RCL_CH_COUNT; i++) {
        int16_t pin = inputPin(i);
        if (pin == RCLIGHTS_PIN_NONE)
            continue;

        volatile uint8_t *pcicr = digitalPinToPCICR((uint8_t)pin);
        volatile uint8_t *pcmsk = digitalPinToPCMSK((uint8_t)pin);
        if (pcicr == nullptr || pcmsk == nullptr)
            return false; /* A pin with no pin-change interrupt behind it. */

        pinMode((uint8_t)pin, INPUT);
        m_avrPort[i] = digitalPinToPCICRbit((uint8_t)pin);
        m_avrLevel[i] = digitalRead((uint8_t)pin);

        uint8_t sreg = SREG;
        cli();
        *pcmsk |= (uint8_t)(1u << digitalPinToPCMSKbit((uint8_t)pin));
        *pcicr |= (uint8_t)(1u << m_avrPort[i]);
        SREG = sreg;
    }
    return true;
}

void RcLights::detachInputs()
{
    for (uint8_t i = 0; i < RCL_CH_COUNT; i++) {
        int16_t pin = inputPin(i);
        if (pin == RCLIGHTS_PIN_NONE)
            continue;
        volatile uint8_t *pcmsk = digitalPinToPCMSK((uint8_t)pin);
        if (pcmsk == nullptr)
            continue;
        uint8_t sreg = SREG;
        cli();
        *pcmsk &= (uint8_t)~(1u << digitalPinToPCMSKbit((uint8_t)pin));
        SREG = sreg;
    }
    /* PCICR is deliberately left alone: another library may be using the same
     * port, and the mask above is what actually silences our pins. */
}

#else /* every other core, and AVR with RCLIGHTS_NO_AVR_PCINT */

bool RcLights::attachInputs()
{
    static void (*const trampolines[RCL_CH_COUNT])() = {isr0, isr1, isr2};

    for (uint8_t i = 0; i < RCL_CH_COUNT; i++) {
        int16_t pin = inputPin(i);
        if (pin == RCLIGHTS_PIN_NONE)
            continue;

        pinMode((uint8_t)pin, INPUT);

        int irq = digitalPinToInterrupt((uint8_t)pin);
        if (irq < 0)
            return false; /* This pin cannot raise an interrupt on this board. */

        attachInterrupt((uint8_t)irq, trampolines[i], CHANGE);
    }
    return true;
}

void RcLights::detachInputs()
{
    for (uint8_t i = 0; i < RCL_CH_COUNT; i++) {
        int16_t pin = inputPin(i);
        if (pin == RCLIGHTS_PIN_NONE)
            continue;
        int irq = digitalPinToInterrupt((uint8_t)pin);
        if (irq >= 0)
            detachInterrupt((uint8_t)irq);
    }
}

#endif
