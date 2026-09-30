/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file rclights_core.h
 * @brief Board-independent light controller for an RC car, driven by three RC
 *        channels.
 *
 * Plain C11: no Arduino, no timers, no I/O. The caller measures three servo
 * pulses, hands them in with a millisecond clock, and reads six brightness
 * values back. Every decision the lights make is therefore reachable from a
 * host test without a board, which is what tests/test_core.c does.
 *
 * Channel 1 is steering and drives the turn signals, channel 2 is throttle and
 * drives brake, reverse and the driving light, channel 3 is a switch
 * (::rcl_aux_mode_t).
 *
 * Pulse widths are microseconds. Stick positions are normalised to
 * ±#RCL_UNIT, so a threshold means the same on every transmitter. Brightness
 * is 0..255 whatever PWM resolution the board uses.
 *
 * There is no speed sensor: "rolling" is inferred from the throttle history
 * over rcl_config_t::coast_ms, so a car being pushed reads as standing still.
 */

#ifndef RCLIGHTS_CORE_H
#define RCLIGHTS_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Full-scale stick deflection, in normalised units. */
#define RCL_UNIT 1000

/** @brief Highest brightness value an output can carry. */
#define RCL_LEVEL_MAX 255

/**
 * @brief How far the readings may spread during automatic centring, in µs.
 *
 * A stick at rest jitters by a few microseconds; one being moved does not, so
 * anything wider means the measurement is thrown away.
 */
#define RCL_CENTER_SPREAD_US 40

/**
 * @brief How far automatic centring may move a centre, in µs.
 *
 * Trim is worth a few tens of microseconds; more than this is a stick being
 * held. A held stick does not move, so the spread check cannot catch it.
 */
#define RCL_CENTER_MAX_SHIFT_US 200

/** @brief Input channels, in the order they are wired. */
typedef enum {
    RCL_CH_STEER = 0, /**< Steering. */
    RCL_CH_THROTTLE,  /**< Throttle. */
    RCL_CH_AUX,       /**< Auxiliary switch. */
    RCL_CH_COUNT      /**< Number of channels. */
} rcl_channel_t;

/** @brief Light outputs, each a brightness from 0 to #RCL_LEVEL_MAX. */
typedef enum {
    RCL_OUT_FRONT = 0,  /**< Park light / driving light, one LED. */
    RCL_OUT_REAR,       /**< Tail light / brake light, one LED. */
    RCL_OUT_REVERSE,    /**< Reversing light. */
    RCL_OUT_TURN_LEFT,  /**< Left turn signal. */
    RCL_OUT_TURN_RIGHT, /**< Right turn signal. */
    RCL_OUT_AUX,        /**< Auxiliary output (light bar, beacon, ...). */
    RCL_OUT_COUNT       /**< Number of outputs. */
} rcl_output_t;

/** @brief What the throttle is currently taken to mean. */
typedef enum {
    RCL_DRIVE_IDLE = 0, /**< Standing, throttle centred. */
    RCL_DRIVE_FORWARD,  /**< Driving forwards. */
    RCL_DRIVE_BRAKE,    /**< Braking, or holding the brake. */
    RCL_DRIVE_REVERSE   /**< Driving backwards. */
} rcl_drive_t;

/**
 * @brief How the ESC treats a backwards stick, which decides when the lights
 *        show brake and when they show reverse.
 */
typedef enum {
    /**
     * Brake, neutral, then reverse: the usual car ESC. A backwards stick while
     * rolling is braking, so the reversing light stays off during a braking
     * manoeuvre.
     */
    RCL_ESC_BRAKE_THEN_REVERSE = 0,
    /** Brake while rolling, then reverse without neutral: crawlers, and many
     * brushless ESCs in "no delay" mode. */
    RCL_ESC_DIRECT_REVERSE
} rcl_esc_mode_t;

/** @brief How channel 3 is read. */
typedef enum {
    RCL_AUX_MODE_OFF = 0, /**< Channel 3 unused; it is not even required to be present. */
    RCL_AUX_MODE_2POS,    /**< Two-position switch: low and high. */
    RCL_AUX_MODE_3POS     /**< Three-position switch: low, middle and high. */
} rcl_aux_mode_t;

/** @brief What one switch position does. */
typedef enum {
    RCL_AUX_ACTION_NONE = 0,   /**< Nothing. */
    RCL_AUX_ACTION_HAZARD,     /**< Both turn signals blink together. */
    RCL_AUX_ACTION_AUX,        /**< ::RCL_OUT_AUX on. */
    RCL_AUX_ACTION_LIGHTS_OFF, /**< Park and driving light off; brake, reverse and
                                    turn signals keep working. */
    RCL_AUX_ACTION_COUNT       /**< Number of actions. */
} rcl_aux_action_t;

/** @brief Which turn signal is running. */
typedef enum {
    RCL_TURN_NONE = 0, /**< Neither. */
    RCL_TURN_LEFT,     /**< Left. */
    RCL_TURN_RIGHT     /**< Right. */
} rcl_turn_t;

/**
 * @brief Endpoints of one channel, as used by rcl_normalize().
 *
 * The two halves are scaled separately, so a stick whose centre is not the mean
 * of its endpoints still reads 0 at rest and ±#RCL_UNIT at the stops.
 */
typedef struct {
    uint16_t min_us;    /**< Pulse at full deflection one way. */
    uint16_t center_us; /**< Pulse at rest. */
    uint16_t max_us;    /**< Pulse at full deflection the other way. */
    bool invert;        /**< Mirror the result, for a reversed channel. */
} rcl_cal_t;

/**
 * @brief Everything adjustable, in one place.
 *
 * Fill it with rcl_config_default() and change what needs changing.
 * rcl_init() validates it, so a nonsense value is refused rather than acted on.
 */
typedef struct {
    /** @brief Endpoints per channel, indexed by ::rcl_channel_t. */
    rcl_cal_t cal[RCL_CH_COUNT];

    /* --- signal ---------------------------------------------------------- */

    /** @brief Pulses shorter than this are not a servo pulse and are discarded. */
    uint16_t pulse_min_us;
    /** @brief Pulses longer than this are not a servo pulse and are discarded. */
    uint16_t pulse_max_us;
    /**
     * @brief Silence on a channel for this long means the link is gone.
     *
     * ::RCL_CH_AUX is exempt while #aux_mode is ::RCL_AUX_MODE_OFF, so a
     * two-channel receiver does not sit in permanent failsafe.
     */
    uint32_t signal_timeout_ms;
    /** @brief Blink the hazards on signal loss rather than going dark. */
    bool failsafe_hazard;
    /**
     * @brief Measure each stick's rest position over this many milliseconds of
     *        signal; 0 uses rcl_cal_t::center_us as given.
     *
     * The window opens on the first valid pulse, not at power-up, so switching
     * the car on before the transmitter still works. Both sticks read as centred
     * until it closes. A measurement that spreads by more than
     * #RCL_CENTER_SPREAD_US, or lands more than #RCL_CENTER_MAX_SHIFT_US from
     * the configured centre, is discarded.
     */
    uint32_t auto_center_ms;

    /* --- steering -------------------------------------------------------- */

    /** @brief Steering below this magnitude counts as centred. */
    int16_t steer_center_band;
    /** @brief Steering at or beyond this magnitude starts a turn signal. */
    int16_t steer_trigger;
    /**
     * @brief Steering below this magnitude ends a running turn signal.
     *
     * Lower than #steer_trigger on purpose: without the hysteresis, holding the
     * stick near the trigger point makes the signal stutter.
     */
    int16_t steer_release;
    /**
     * @brief How long the steering must have been centred before a deflection
     *        counts as indicating.
     *
     * What separates turning a corner from correcting the line.
     */
    uint32_t center_hold_ms;
    /** @brief Once started, a turn signal runs at least this long. */
    uint32_t turn_min_ms;
    /** @brief One full blink cycle, on plus off. */
    uint32_t blink_period_ms;
    /** @brief Share of the cycle the lamp is lit, in percent. */
    uint8_t blink_duty;

    /* --- throttle -------------------------------------------------------- */

    /** @brief Throttle below this magnitude counts as centred. */
    int16_t throttle_center_band;
    /** @brief Backwards throttle at or beyond this magnitude counts as braking. */
    int16_t brake_threshold;
    /** @brief Which reverse behaviour the ESC has. */
    rcl_esc_mode_t esc_mode;
    /**
     * @brief How long the car is assumed to keep rolling after lift-off.
     *
     * Stands in for the speed sensor there is not. While it runs, a backwards
     * stick is braking; after it, the same stick means reverse.
     */
    uint32_t coast_ms;
    /** @brief Backwards stick must be held this long before reverse is shown. */
    uint32_t reverse_arm_ms;
    /** @brief The brake light stays lit this long after braking ends. */
    uint32_t brake_extend_ms;

    /* --- channel 3 ------------------------------------------------------- */

    /** @brief How channel 3 is read. */
    rcl_aux_mode_t aux_mode;
    /**
     * @brief What each switch position does: index 0 low, 1 middle, 2 high.
     *
     * The middle entry is unused in ::RCL_AUX_MODE_2POS.
     */
    rcl_aux_action_t aux_action[3];
    /** @brief Channel 3 at or below this is the low position. */
    int16_t aux_low;
    /** @brief Channel 3 at or above this is the high position. */
    int16_t aux_high;

    /* --- brightness ------------------------------------------------------ */

    /** @brief Front LED standing still. */
    uint8_t level_front_park;
    /** @brief Front LED while driving. */
    uint8_t level_front_drive;
    /** @brief Rear LED not braking. */
    uint8_t level_rear_park;
    /** @brief Rear LED while braking. */
    uint8_t level_rear_brake;
    /** @brief Reversing light when lit. */
    uint8_t level_reverse;
    /** @brief Turn signal when lit. */
    uint8_t level_turn;
    /** @brief Auxiliary output when on. */
    uint8_t level_aux;
    /** @brief Park and tail light on whenever the link is up. */
    bool park_lights_on;
    /**
     * @brief Brightness steps per 10 ms for the front and rear LED; 0 is
     *        instant.
     *
     * Only those two fade; the turn signals, reversing light and auxiliary
     * output always switch instantly. The default takes about 60 ms, which
     * reads as a bulb warming rather than as a delay.
     */
    uint8_t fade_step;
} rcl_config_t;

/**
 * @brief One set of measurements, as handed to rcl_update().
 */
typedef struct {
    /** @brief Last measured pulse per channel, in microseconds. */
    uint16_t pulse_us[RCL_CH_COUNT];
    /**
     * @brief Whether that measurement is new since the previous rcl_update().
     *
     * Needed for the timeout: a channel repeating its last value forever is
     * what a dead receiver output looks like.
     */
    bool fresh[RCL_CH_COUNT];
} rcl_input_t;

/**
 * @brief Controller state. Opaque in practice — read it through the accessors.
 */
typedef struct {
    rcl_config_t cfg; /**< Active configuration, copied at rcl_init(). */

    uint32_t last_ms;                    /**< Clock at the previous update. */
    bool started;                        /**< rcl_init() has run. */
    int16_t norm[RCL_CH_COUNT];          /**< Normalised stick positions. */
    bool chan_valid[RCL_CH_COUNT];       /**< Channel seen within the timeout. */
    uint32_t last_seen_ms[RCL_CH_COUNT]; /**< Clock at the last fresh pulse. */
    bool failsafe;                       /**< A required channel is missing. */

    /** @brief Clock when automatic centring started, per centred channel. */
    uint32_t center_start_ms[2];
    /** @brief Sum of the readings taken so far, per centred channel. */
    uint32_t center_sum[2];
    /** @brief Number of readings taken so far, per centred channel. */
    uint16_t center_count[2];
    /** @brief Smallest reading seen, per centred channel. */
    uint16_t center_lo[2];
    /** @brief Largest reading seen, per centred channel. */
    uint16_t center_hi[2];
    /** @brief Centring has finished, per centred channel. */
    bool center_done[2];

    rcl_drive_t drive;     /**< Current drive state. */
    uint32_t coast_left;   /**< Remaining assumed roll-out, ms. */
    uint32_t back_held_ms; /**< Backwards stick held, ms. */
    uint32_t brake_left;   /**< Remaining brake light hold, ms. */
    bool reverse_armed;    /**< Neutral was seen since the last brake. */

    uint32_t centered_ms; /**< Steering has been centred this long. */
    bool turn_armed;      /**< Centred long enough for the next deflection to count. */
    rcl_turn_t turn;      /**< Running turn signal. */
    uint32_t turn_ms;     /**< That signal has been running this long. */
    bool turn_ending;     /**< Stop once the current blink cycle finishes. */

    uint8_t aux_pos; /**< Last switch position read: 0 low, 1 middle, 2 high. */
    bool hazard;     /**< Hazards requested by channel 3. */
    bool aux_on;     /**< Auxiliary output requested by channel 3. */
    bool lights_off; /**< Park light suppressed by channel 3. */

    uint32_t blink_ms; /**< Position within the blink cycle. */
    bool blink_on;     /**< Lit half of the cycle. */

    uint8_t target[RCL_OUT_COUNT]; /**< Where each output is heading. */
    uint8_t out[RCL_OUT_COUNT];    /**< Where each output is now. */
} rcl_state_t;

/**
 * @brief Fill a configuration with the defaults.
 *
 * Sensible for a 1/10 on-road car with a car-style ESC and a three-position
 * switch on channel 3 doing nothing / auxiliary / hazards.
 *
 * @param cfg Configuration to fill. Ignored when NULL.
 */
void rcl_config_default(rcl_config_t *cfg);

/**
 * @brief Check a configuration for values that cannot be acted on.
 *
 * Checks the things that would otherwise fail silently or divide by zero:
 * endpoint ordering, thresholds within range, release below trigger, a blink
 * period that is not zero, a duty cycle from 1 to 99, and aux thresholds in
 * order.
 *
 * @param cfg Configuration to check.
 * @return true when every field can be acted on.
 */
bool rcl_config_validate(const rcl_config_t *cfg);

/**
 * @brief Normalise one pulse width against its calibration.
 *
 * The two halves are scaled separately, so an off-centre trim does not skew
 * full deflection. The result is clamped to ±#RCL_UNIT.
 *
 * @param cal Endpoints for this channel.
 * @param pulse_us Measured pulse, in microseconds.
 * @return Stick position from -#RCL_UNIT to +#RCL_UNIT.
 */
int16_t rcl_normalize(const rcl_cal_t *cal, uint16_t pulse_us);

/**
 * @brief Start the controller.
 *
 * @param st State to initialise.
 * @param cfg Configuration; copied, so the caller may discard it afterwards.
 *            NULL means rcl_config_default().
 * @param now_ms Current millisecond clock.
 * @return true on success, false when @p st is NULL or @p cfg does not pass
 *         rcl_config_validate(), in which case nothing is changed.
 */
bool rcl_init(rcl_state_t *st, const rcl_config_t *cfg, uint32_t now_ms);

/**
 * @brief Advance the controller by one step and recompute every output.
 *
 * Call as often as convenient; 100 Hz or faster keeps the blink phase and the
 * fade smooth. The elapsed time is taken from @p now_ms, so an irregular call
 * rate changes nothing but the resolution. A clock that has wrapped past 2^32
 * is handled by unsigned arithmetic and needs no special case.
 *
 * @param st State, previously passed to rcl_init().
 * @param in Measurements. NULL is treated as "nothing fresh", which after
 *           rcl_config_t::signal_timeout_ms puts the controller into failsafe.
 * @param now_ms Current millisecond clock.
 */
void rcl_update(rcl_state_t *st, const rcl_input_t *in, uint32_t now_ms);

/**
 * @brief Read one output.
 * @param st State.
 * @param out Which output.
 * @return Brightness from 0 to #RCL_LEVEL_MAX, or 0 for a bad argument.
 */
uint8_t rcl_output(const rcl_state_t *st, rcl_output_t out);

/**
 * @brief Read the normalised position of one channel.
 * @param st State.
 * @param ch Which channel.
 * @return Position from -#RCL_UNIT to +#RCL_UNIT, or 0 for a bad argument.
 */
int16_t rcl_channel(const rcl_state_t *st, rcl_channel_t ch);

/**
 * @brief Whether automatic centring is still running.
 * @param st State.
 * @return true while either stick is still being measured.
 */
bool rcl_centering(const rcl_state_t *st);

/**
 * @brief Name a drive state, for logging.
 * @param drive Drive state.
 * @return A static string; "?" for a bad argument.
 */
const char *rcl_drive_name(rcl_drive_t drive);

#ifdef __cplusplus
}
#endif

#endif /* RCLIGHTS_CORE_H */
