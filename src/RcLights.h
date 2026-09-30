/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLights.h
 * @brief Arduino front end for the RC car light controller.
 *
 * Measures three receiver channels with interrupts, hands them to the portable
 * core in `rclights_core.h`, and writes the six brightness values it gets back
 * out as PWM. That is the whole job; every decision about what the lights do
 * lives in the core and can be read, and tested, without a board.
 *
 * @par The shortest complete sketch
 * @code
 * #include <RcLights.h>
 *
 * RcLights lights;
 *
 * void setup()
 * {
 *     lights.begin();          // default pins for this board
 * }
 *
 * void loop()
 * {
 *     lights.loop();           // call as often as you like
 * }
 * @endcode
 *
 * @par One instance
 * The capture runs from interrupt handlers that reach the object through a
 * static pointer, so exactly one RcLights may be `begin()`-ed at a time. A
 * second call to begin() on another object fails and returns false rather than
 * quietly stealing the first one's interrupts.
 *
 * @par AVR and its two interrupt pins
 * An ATmega328P can attach an interrupt to D2 and D3 and to nothing else, which
 * is one short of the three channels this needs. On AVR the library therefore
 * installs its own pin-change interrupt handlers and defines `PCINT0_vect`,
 * `PCINT1_vect` and `PCINT2_vect`. Another library that does the same — several
 * softserial and RC receiver libraries do — will not link alongside it. Define
 * `RCLIGHTS_NO_AVR_PCINT` to fall back to `attachInterrupt()`, and then wire the
 * channels you care about to D2 and D3.
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

/**
 * @brief Three RC channels in, six light outputs out.
 *
 * @par Settings
 * RcLights *is* an ::rcl_config_t — it derives from one — so every setting the
 * controller has is a field of the object, spelled exactly as that structure
 * spells it:
 *
 * @code
 * lights.esc_mode = RCL_ESC_DIRECT_REVERSE;
 * lights.level_rear_brake = 255;
 * lights.cal[RCL_CH_STEER].invert = true;
 * lights.begin();
 * @endcode
 *
 * The constructor fills them in with rcl_config_default(), so a sketch only
 * writes the ones it wants changed. They are read by begin(); to change one
 * while running, write it and call apply().
 *
 * That inheritance is why there is no list of settings here to fall out of date
 * with the controller's: adding a field to ::rcl_config_t adds it to this class
 * at the same moment. The one setting that is not in there is #outputs, because
 * LED polarity is a fact about the wiring rather than about the controller.
 */
class RcLights : public rcl_config_t
{
public:
    RcLights();
    ~RcLights();

    /**
     * @brief Which level lights your LEDs.
     *
     * Read by every begin(), and applied before the outputs are first driven —
     * so an active-low string does not flash at full brightness on the way up.
     * For a car with some LEDs one way round and some the other, leave this at
     * ::RCLIGHTS_OUTPUTS_ACTIVE_HIGH and call setInvertedOutputs() after
     * begin().
     */
    RcLightsOutputs outputs;

    /**
     * @brief Start with this board's default pins and the settings on the
     *        object.
     *
     * Defined here rather than in the library so that a sketch which defines a
     * pin macro before including this header gets the pin it asked for; see
     * rclights_board.h.
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
     * @brief Start with your own pins and a configuration from somewhere else.
     *
     * For a sketch that keeps its settings in an ::rcl_config_t of its own — one
     * read back from EEPROM, say. @p cfg replaces the settings on the object, so
     * afterwards they read back as what was actually applied.
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
     * @return true when they were accepted; false leaves the running settings
     *         alone, and the fields then disagree with them until they are
     *         corrected or apply() succeeds.
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
     * Call from `loop()` as often as convenient. It does not block and does not
     * need a fixed rate; 100 Hz or better keeps the blink phase and the fade
     * smooth. Calling it far more often than that costs nothing beyond the
     * reads, because the core works from the millisecond clock rather than from
     * a call count.
     */
    void loop();

    /**
     * @brief Replace the settings while running, from somewhere else.
     *
     * Like apply(), but taking the settings as an argument; @p cfg replaces the
     * fields on the object as well.
     *
     * @param cfg New settings.
     * @return true when they were accepted; false leaves the old ones in place.
     */
    bool applyConfig(const rcl_config_t &cfg);

    /**
     * @brief The settings actually in force.
     *
     * The same values as the object's own fields, except in the window between
     * writing a field and calling apply(), and after an apply() that was
     * refused. Reading this rather than the fields answers "what is the
     * controller doing", not "what have I asked for".
     *
     * @return A reference to the live configuration.
     */
    const rcl_config_t &config() const;

    /**
     * @brief The pin assignment in force.
     *
     * Useful mostly to print this board's defaults rather than looking them up.
     *
     * @return A read-only reference.
     */
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
     * @brief The controller state, for anything the accessors do not cover.
     * @return A read-only reference.
     */
    const rcl_state_t &state() const;

    /**
     * @brief Invert individual outputs, for a car wired both ways round.
     *
     * #outputs covers the usual case, where every LED is the same way round.
     * This is the per-output form: one bit per ::rcl_output_t, bit 0 being
     * ::RCL_OUT_FRONT. Call it after begin(), which sets the mask from
     * #outputs.
     *
     * @param mask Bit set per inverted output; 0 for none.
     */
    void setInvertedOutputs(uint8_t mask);

    /**
     * @brief Set the PWM carrier frequency where the board allows it.
     *
     * Takes effect at the next begin(). Ignored on AVR, whose PWM frequency is
     * fixed by the timer prescaler and shared with `millis()`.
     *
     * @param hz Frequency in Hz.
     */
    void setPwmFrequency(uint32_t hz);

    /**
     * @brief Take the current stick positions as the centre of steering and
     *        throttle.
     *
     * Hold the transmitter sticks at rest and call this once. Only those two
     * channels are touched — channel 3 is a switch and has no centre — and only
     * the centres move; the endpoints stay as configured. Nothing is stored
     * anywhere, so a sketch that wants this to survive a power cycle has to save
     * the configuration itself.
     *
     * @return true when both channels were live and the centres were taken;
     *         false leaves the calibration untouched.
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
     * @brief Entry point for the AVR pin-change vectors.
     *
     * Public only because the `ISR()` macro expands to a free function that has
     * to be able to call it. Not part of the interface; do not call it.
     *
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
