/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLights.h
 * @brief Arduino front end: capture three receiver channels, drive six PWM
 *        outputs. Every decision about the lights lives in rclights_core.h.
 *
 * @code
 * RcLights lights;
 * void setup() { lights.begin(); }
 * void loop()  { lights.loop(); }
 * @endcode
 *
 * The interrupt handlers reach the object through a static pointer, so only one
 * instance may be begin()-ed at a time; a second call returns false.
 *
 * On AVR the library installs its own pin-change handlers and defines
 * `PCINT0_vect`, `PCINT1_vect` and `PCINT2_vect`, so it will not link alongside
 * another library that does the same. `RCLIGHTS_NO_AVR_PCINT` falls back to
 * `attachInterrupt()` on D2 and D3.
 */

#ifndef RCLIGHTS_H
#define RCLIGHTS_H

#include <Arduino.h>

#include "rclights_board.h"
#include "rclights_core.h"

/** @brief Which level lights an LED. */
enum RcLightsOutputs {
    /** @brief LEDs switched to ground, lit by a high level. The usual case. */
    RCLIGHTS_OUTPUTS_ACTIVE_HIGH = 0,
    /** @brief LEDs wired to the supply rail, lit by a low level. */
    RCLIGHTS_OUTPUTS_ACTIVE_LOW
};

/*
 * Short spellings of the values a sketch types, as the Arduino style guide asks
 * for. The long RCL_ names are the C core's and keep working; these cost no
 * storage, and each is checked against what it aliases in
 * tests/test_arduino_port.cpp.
 */

/** @brief LEDs switched to ground. @see RCLIGHTS_OUTPUTS_ACTIVE_HIGH */
const RcLightsOutputs LEDS_ACTIVE_HIGH = RCLIGHTS_OUTPUTS_ACTIVE_HIGH;
/** @brief LEDs wired to the supply rail. @see RCLIGHTS_OUTPUTS_ACTIVE_LOW */
const RcLightsOutputs LEDS_ACTIVE_LOW = RCLIGHTS_OUTPUTS_ACTIVE_LOW;

/** @brief Back stick brakes; reverse needs neutral. @see RCL_ESC_BRAKE_THEN_REVERSE */
const rcl_esc_mode_t ESC_BRAKE_THEN_REVERSE = RCL_ESC_BRAKE_THEN_REVERSE;
/** @brief Back stick reverses out of the brake. @see RCL_ESC_DIRECT_REVERSE */
const rcl_esc_mode_t ESC_DIRECT_REVERSE = RCL_ESC_DIRECT_REVERSE;

/** @brief No third channel. @see RCL_AUX_MODE_OFF */
const rcl_aux_mode_t SWITCH_NONE = RCL_AUX_MODE_OFF;
/** @brief A two-position switch. @see RCL_AUX_MODE_2POS */
const rcl_aux_mode_t SWITCH_2POS = RCL_AUX_MODE_2POS;
/** @brief A three-position switch. @see RCL_AUX_MODE_3POS */
const rcl_aux_mode_t SWITCH_3POS = RCL_AUX_MODE_3POS;

/** @brief Switch position does nothing. @see RCL_AUX_ACTION_NONE */
const rcl_aux_action_t ACTION_NOTHING = RCL_AUX_ACTION_NONE;
/** @brief Switch position raises the hazards. @see RCL_AUX_ACTION_HAZARD */
const rcl_aux_action_t ACTION_HAZARDS = RCL_AUX_ACTION_HAZARD;
/** @brief Switch position drives the auxiliary output. @see RCL_AUX_ACTION_AUX */
const rcl_aux_action_t ACTION_AUX = RCL_AUX_ACTION_AUX;
/** @brief Switch position kills the park lights. @see RCL_AUX_ACTION_LIGHTS_OFF */
const rcl_aux_action_t ACTION_LIGHTS_OFF = RCL_AUX_ACTION_LIGHTS_OFF;

/**
 * @brief Three RC channels in, six light outputs out.
 *
 * RcLights derives from ::rcl_config_t, so every setting is a field of the
 * object, spelled as that structure spells it. There is no second list here to
 * fall out of step with it.
 *
 * @code
 * lights.esc_mode = ESC_DIRECT_REVERSE;
 * lights.level_rear_brake = 255;
 * lights.begin();
 * @endcode
 *
 * The constructor fills them from rcl_config_default(); begin() reads them, and
 * apply() puts a later change into force. #outputs is the one setting not in
 * there, because LED polarity describes the wiring, not the controller.
 */
class RcLights : public rcl_config_t
{
public:
    RcLights();
    ~RcLights();

    /**
     * @brief Which level lights your LEDs.
     *
     * Applied before the outputs are first driven, so an active-low string does
     * not flash at full brightness on the way up. For a car wired both ways
     * round, leave it and call setInvertedOutputs() after begin().
     */
    RcLightsOutputs outputs;

    /**
     * @brief Start with this board's default pins and the settings on the object.
     *
     * Defined here, not in the library, so a sketch that #defines a pin macro
     * above the include gets the pin it asked for.
     *
     * @return true on success; false when the settings do not pass
     *         rcl_config_validate(), or another instance is already running.
     */
    bool begin()
    {
        const RcLightsPins defaults = RCLIGHTS_PINS_DEFAULT;
        return begin(defaults);
    }

    /**
     * @brief Start with your own pins and the settings on the object.
     * @param pins Pin assignment; #RCLIGHTS_PIN_NONE for anything unconnected.
     * @return true on success.
     */
    bool begin(const RcLightsPins &pins);

    /**
     * @brief Start with your own pins and settings from elsewhere.
     *
     * @p cfg replaces the settings on the object, so they afterwards read back
     * as what was applied.
     *
     * @param pins Pin assignment.
     * @param cfg Settings, as prepared by rcl_config_default().
     * @return true on success; false when @p cfg does not pass
     *         rcl_config_validate() or another instance is already running.
     */
    bool begin(const RcLightsPins &pins, const rcl_config_t &cfg);

    /**
     * @brief Put the settings currently on the object into force.
     *
     * For changing something while running: write the field, call this.
     *
     * @return true when accepted; false leaves the running settings alone.
     */
    bool apply();

    /**
     * @brief Release the interrupts and switch every output off.
     *
     * Safe to call when not running.
     */
    void end();

    /**
     * @brief Read the channels, advance the controller, write the outputs.
     *
     * Does not block and needs no fixed rate; 100 Hz or better keeps the blink
     * and the fade smooth. The core works from millis(), not from a call count,
     * so calling it more often costs only the reads.
     */
    void loop();

    /**
     * @brief Replace the settings while running, from elsewhere.
     *
     * Like apply(), but @p cfg replaces the fields on the object as well.
     *
     * @param cfg New settings.
     * @return true when accepted; false leaves the old ones in place.
     */
    bool applyConfig(const rcl_config_t &cfg);

    /**
     * @brief The settings actually in force.
     *
     * Differs from the object's fields only between writing one and calling
     * apply(), and after an apply() that was refused.
     *
     * @return A reference to the live configuration.
     */
    const rcl_config_t &config() const;

    /** @brief The pin assignment in force. @return A read-only reference. */
    const RcLightsPins &pins() const;

    /**
     * @brief Last measured pulse on one channel.
     * @param ch Channel.
     * @return Pulse width in microseconds, or 0 when nothing has been measured.
     */
    uint16_t pulseUs(rcl_channel_t ch) const;

    /**
     * @brief Whether a channel has been heard from within the timeout.
     * @param ch Channel.
     * @return true when the channel is live.
     */
    bool channelValid(rcl_channel_t ch) const;

    /**
     * @brief Stick position on one channel.
     * @param ch Channel.
     * @return -#RCL_UNIT to +#RCL_UNIT.
     */
    int16_t channel(rcl_channel_t ch) const;

    /**
     * @brief Current brightness of one output.
     * @param out Output.
     * @return 0 to #RCL_LEVEL_MAX, as written to the pin before inversion.
     */
    uint8_t output(rcl_output_t out) const;

    /**
     * @brief What the throttle is currently taken to mean.
     * @return The drive state.
     */
    rcl_drive_t drive() const;

    /**
     * @brief Whether the link is down.
     * @return true while a required channel has gone quiet.
     */
    bool failsafe() const;

    /**
     * @brief Whether the stick centres are still being measured.
     *
     * True for the first rcl_config_t::auto_center_ms of signal, during which
     * both sticks read as centred. Nothing needs to wait for it.
     *
     * @return true while either stick is still being measured.
     */
    bool centering() const;

    /**
     * @brief The controller state, for anything the accessors do not cover.
     * @return A read-only reference.
     */
    const rcl_state_t &state() const;

    /**
     * @brief Invert individual outputs, for a car wired both ways round.
     *
     * One bit per ::rcl_output_t, bit 0 being ::RCL_OUT_FRONT. Call it after
     * begin(), which sets the mask from #outputs.
     *
     * @param mask Bit set per inverted output; 0 for none.
     */
    void setInvertedOutputs(uint8_t mask);

    /**
     * @brief Set the PWM carrier frequency where the board allows it.
     *
     * Takes effect at the next begin(). Ignored on AVR, where it is fixed by
     * the timer prescaler.
     *
     * @param hz Frequency in Hz.
     */
    void setPwmFrequency(uint32_t hz);

    /**
     * @brief Take the stick positions as the centre of steering and throttle.
     *
     * Only those two are touched; channel 3 is a switch and has no centre. Only
     * the centres move, and nothing is stored, so a sketch that wants this to
     * survive a power cycle saves the configuration itself.
     *
     * @return true when both channels were live and the centres were taken.
     */
    bool captureCenter();

private:
    /** @brief One channel's capture state, written from an interrupt. */
    struct Capture {
        volatile uint32_t rise_us;  /**< micros() at the rising edge. */
        volatile uint16_t pulse_us; /**< Width of the last complete pulse. */
        volatile bool fresh;        /**< A pulse arrived since the last read. */
    };

    /** @brief Install the input interrupts. @return true on success. */
    bool attachInputs();
    /** @brief Remove the input interrupts again. */
    void detachInputs();
    /** @brief Write every output whose value has changed. */
    void writeOutputs();
    /**
     * @brief Drive one pin, applying inversion.
     * @param out Which output.
     * @param level Brightness before inversion.
     */
    void writePin(rcl_output_t out, uint8_t level);
    /**
     * @brief The pin carrying one output.
     * @param out Which output.
     * @return Pin number, or #RCLIGHTS_PIN_NONE.
     */
    int16_t outputPin(rcl_output_t out) const;
    /**
     * @brief The pin carrying one input channel.
     * @param ch Which channel.
     * @return Pin number, or #RCLIGHTS_PIN_NONE.
     */
    int16_t inputPin(uint8_t ch) const;

    /**
     * @brief Handle one edge, reading the level from the pin.
     * @param idx Channel index.
     */
    void onEdge(uint8_t idx);
    /**
     * @brief Handle one edge whose level is already known.
     * @param idx Channel index.
     * @param high true for a rising edge.
     */
    void onEdgeLevel(uint8_t idx, bool high);

    /** @brief Interrupt trampoline for channel 0. */
    static void RCLIGHTS_ISR_ATTR isr0();
    /** @brief Interrupt trampoline for channel 1. */
    static void RCLIGHTS_ISR_ATTR isr1();
    /** @brief Interrupt trampoline for channel 2. */
    static void RCLIGHTS_ISR_ATTR isr2();

    /** @brief The one instance whose interrupts are installed, or nullptr. */
    static RcLights *s_active;

#if defined(ARDUINO_ARCH_AVR) && !defined(RCLIGHTS_NO_AVR_PCINT)
    /** @brief Port index (0 = B, 1 = C, 2 = D) per input channel. */
    uint8_t m_avrPort[RCL_CH_COUNT];
    /** @brief Last seen level per input channel, to tell the pins apart. */
    uint8_t m_avrLevel[RCL_CH_COUNT];

public:
    /**
     * @brief Entry point for the AVR pin-change vectors. Not part of the
     *        interface; public only because ISR() expands to a free function.
     * @param port 0 for PORTB, 1 for PORTC, 2 for PORTD.
     */
    static void avrIsrEntry(uint8_t port);

private:
#endif

    rcl_state_t m_state;              /**< The controller. */
    RcLightsPins m_pins;              /**< Pin assignment in force. */
    Capture m_cap[RCL_CH_COUNT];      /**< Capture state per input channel. */
    uint16_t m_pulse[RCL_CH_COUNT];   /**< Last pulse handed to the core. */
    uint8_t m_invert;                 /**< Inverted-output mask. */
    uint8_t m_written[RCL_OUT_COUNT]; /**< Last value written per output. */
    uint32_t m_pwmHz;                 /**< Requested carrier frequency. */
    bool m_running;                   /**< begin() has succeeded and end() has not run. */
};

#endif /* RCLIGHTS_H */
