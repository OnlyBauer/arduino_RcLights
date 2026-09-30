/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file rclights_core.c
 * @brief Implementation of the board-independent light controller.
 *
 * Integer arithmetic throughout, no allocation, no I/O and no dependency beyond
 * `<string.h>` for memset. The heaviest operation in the whole file is a 32-bit
 * divide in rcl_normalize(), which runs once per channel per update; that is
 * within reach of an ATmega328P at 16 MHz with room to spare.
 *
 * The order of the steps in rcl_update() is load-bearing and is called out at
 * each one.
 */

#include "rclights_core.h"

#include <string.h>

/** @brief Largest step the clock may advance in one update, in ms. */
#define RCL_DT_MAX_MS 1000u

/** @brief Shortest pulse that could plausibly be a servo frame, in µs. */
#define RCL_PULSE_FLOOR_US 500u
/** @brief Longest pulse that could plausibly be a servo frame, in µs. */
#define RCL_PULSE_CEIL_US 2500u

/**
 * @brief Count a timer down without letting it wrap.
 * @param value Remaining time, ms.
 * @param dt Elapsed time, ms.
 * @return The remainder, or 0.
 */
static uint32_t countdown(uint32_t value, uint32_t dt)
{
    return value > dt ? value - dt : 0u;
}

/**
 * @brief Absolute value of a normalised stick position.
 * @param v Position.
 * @return Magnitude.
 */
static int16_t magnitude(int16_t v)
{
    return v < 0 ? (int16_t)-v : v;
}

void rcl_config_default(rcl_config_t *cfg)
{
    if (!cfg)
        return;

    memset(cfg, 0, sizeof(*cfg));

    for (int i = 0; i < RCL_CH_COUNT; i++) {
        cfg->cal[i].min_us = 1000;
        cfg->cal[i].center_us = 1500;
        cfg->cal[i].max_us = 2000;
        cfg->cal[i].invert = false;
    }

    /* Wider than the nominal 1000..2000 because receivers overshoot at the
     * endpoints, but far enough inside the frame period that a missed edge
     * cannot be mistaken for a pulse. */
    cfg->pulse_min_us = 700;
    cfg->pulse_max_us = 2300;
    /* About eleven frames at 50 Hz. Long enough that a few dropped frames on a
     * marginal link do not flash the hazards, short enough to react before the
     * car has gone far. */
    cfg->signal_timeout_ms = 500;
    cfg->failsafe_hazard = true;
    /* Ten frames at 50 Hz: long enough to average out the jitter and to notice
     * a stick that is being held, short enough that nobody sees the car wait
     * for it. */
    cfg->auto_center_ms = 200;

    cfg->steer_center_band = 120;
    cfg->steer_trigger = 350;
    cfg->steer_release = 200;
    cfg->center_hold_ms = 400;
    cfg->turn_min_ms = 900;
    /* 700 ms is about 86 flashes a minute, inside the 60..120 that road
     * vehicle regulations ask for and close to what a real car does. */
    cfg->blink_period_ms = 700;
    cfg->blink_duty = 50;

    cfg->throttle_center_band = 100;
    cfg->brake_threshold = 200;
    cfg->esc_mode = RCL_ESC_BRAKE_THEN_REVERSE;
    cfg->coast_ms = 1500;
    cfg->reverse_arm_ms = 250;
    cfg->brake_extend_ms = 400;

    cfg->aux_mode = RCL_AUX_MODE_3POS;
    cfg->aux_action[0] = RCL_AUX_ACTION_NONE;
    cfg->aux_action[1] = RCL_AUX_ACTION_AUX;
    cfg->aux_action[2] = RCL_AUX_ACTION_HAZARD;
    cfg->aux_low = -300;
    cfg->aux_high = 300;

    cfg->level_front_park = 40;
    cfg->level_front_drive = 255;
    cfg->level_rear_park = 30;
    cfg->level_rear_brake = 255;
    cfg->level_reverse = 255;
    cfg->level_turn = 255;
    cfg->level_aux = 255;
    cfg->park_lights_on = true;
    /* 40 steps per 10 ms is the full range in about 64 ms: visibly a lamp
     * warming up rather than a delay. */
    cfg->fade_step = 40;
}

/**
 * @brief Check one channel's endpoints.
 * @param cal Endpoints.
 * @return true when they are ordered and plausible.
 */
static bool cal_ok(const rcl_cal_t *cal)
{
    if (cal->min_us < RCL_PULSE_FLOOR_US || cal->max_us > RCL_PULSE_CEIL_US)
        return false;
    /* Strictly ordered, and with enough span either side that normalising does
     * not divide by something close to zero. */
    if (cal->center_us <= cal->min_us + 50u || cal->max_us <= cal->center_us + 50u)
        return false;
    return true;
}

bool rcl_config_validate(const rcl_config_t *cfg)
{
    if (!cfg)
        return false;

    for (int i = 0; i < RCL_CH_COUNT; i++) {
        if (!cal_ok(&cfg->cal[i]))
            return false;
    }

    if (cfg->pulse_min_us < RCL_PULSE_FLOOR_US || cfg->pulse_max_us > RCL_PULSE_CEIL_US)
        return false;
    if (cfg->pulse_min_us + 100u > cfg->pulse_max_us)
        return false;
    if (cfg->signal_timeout_ms == 0u)
        return false;
    /* Zero means off. An upper bound because this is time the car spends
     * refusing to act on its sticks, and a minute of that is a fault, not a
     * setting. */
    if (cfg->auto_center_ms > 5000u)
        return false;

    if (cfg->steer_center_band < 0 || cfg->steer_center_band >= RCL_UNIT)
        return false;
    /* Release above the centre band, trigger at or above release: without both,
     * a signal could either never start or never stop. */
    if (cfg->steer_release <= cfg->steer_center_band || cfg->steer_release > RCL_UNIT)
        return false;
    if (cfg->steer_trigger < cfg->steer_release || cfg->steer_trigger > RCL_UNIT)
        return false;

    if (cfg->blink_period_ms == 0u)
        return false;
    if (cfg->blink_duty == 0u || cfg->blink_duty >= 100u)
        return false;

    if (cfg->throttle_center_band < 0 || cfg->throttle_center_band >= RCL_UNIT)
        return false;
    if (cfg->brake_threshold < cfg->throttle_center_band || cfg->brake_threshold > RCL_UNIT)
        return false;
    if (cfg->esc_mode != RCL_ESC_BRAKE_THEN_REVERSE && cfg->esc_mode != RCL_ESC_DIRECT_REVERSE)
        return false;

    if (cfg->aux_mode != RCL_AUX_MODE_OFF && cfg->aux_mode != RCL_AUX_MODE_2POS &&
        cfg->aux_mode != RCL_AUX_MODE_3POS)
        return false;
    for (int i = 0; i < 3; i++) {
        if ((int)cfg->aux_action[i] < 0 || cfg->aux_action[i] >= RCL_AUX_ACTION_COUNT)
            return false;
    }
    if (cfg->aux_low >= cfg->aux_high)
        return false;
    if (cfg->aux_low < -RCL_UNIT || cfg->aux_high > RCL_UNIT)
        return false;

    return true;
}

int16_t rcl_normalize(const rcl_cal_t *cal, uint16_t pulse_us)
{
    if (!cal)
        return 0;

    int32_t p = (int32_t)pulse_us;
    int32_t centre = (int32_t)cal->center_us;
    int32_t span;

    /* The two halves are scaled separately so that trim on the transmitter
     * moves the rest point without skewing full deflection. */
    if (p >= centre)
        span = (int32_t)cal->max_us - centre;
    else
        span = centre - (int32_t)cal->min_us;

    int32_t v = span > 0 ? ((p - centre) * RCL_UNIT) / span : 0;

    if (v > RCL_UNIT)
        v = RCL_UNIT;
    if (v < -RCL_UNIT)
        v = -RCL_UNIT;
    if (cal->invert)
        v = -v;

    return (int16_t)v;
}

bool rcl_init(rcl_state_t *st, const rcl_config_t *cfg, uint32_t now_ms)
{
    if (!st)
        return false;

    rcl_config_t use;
    if (cfg)
        use = *cfg;
    else
        rcl_config_default(&use);

    if (!rcl_config_validate(&use))
        return false;

    memset(st, 0, sizeof(*st));
    st->cfg = use;
    st->last_ms = now_ms;
    st->started = true;
    /* No channel has been seen yet, so the controller starts in failsafe and
     * stays there until the receiver produces pulses. A light controller that
     * powered up assuming a good link would show park lights on a car whose
     * transmitter is still off. */
    st->failsafe = true;
    for (int i = 0; i < RCL_CH_COUNT; i++)
        st->last_seen_ms[i] = now_ms;

    /* With the window set to zero the configured centres are used as given and
     * the sticks are live from the first pulse. */
    for (int i = 0; i < 2; i++)
        st->center_done[i] = use.auto_center_ms == 0u;

    return true;
}

/**
 * @brief Fold one reading into a channel's centring measurement, and finish it
 *        once the window has passed.
 *
 * Only the steering and throttle are centred. A switch has no rest position:
 * whichever way it happens to be flicked at power-up would become its centre.
 *
 * @param st State.
 * @param ch Channel, ::RCL_CH_STEER or ::RCL_CH_THROTTLE.
 * @param pulse_us The reading.
 * @param now_ms Current clock.
 */
static void step_center(rcl_state_t *st, int ch, uint16_t pulse_us, uint32_t now_ms)
{
    rcl_cal_t *cal = &st->cfg.cal[ch];

    if (st->center_count[ch] == 0u) {
        st->center_start_ms[ch] = now_ms;
        st->center_lo[ch] = pulse_us;
        st->center_hi[ch] = pulse_us;
    }
    if (pulse_us < st->center_lo[ch])
        st->center_lo[ch] = pulse_us;
    if (pulse_us > st->center_hi[ch])
        st->center_hi[ch] = pulse_us;

    st->center_sum[ch] += pulse_us;
    /* The count cannot realistically overflow -- the window is capped at five
     * seconds and a receiver produces fifty frames a second -- but a caller
     * that feeds this in a tight loop would get there, and a wrapped count
     * divides by the wrong number. */
    if (st->center_count[ch] < 0xFFFFu)
        st->center_count[ch]++;

    if (now_ms - st->center_start_ms[ch] < st->cfg.auto_center_ms)
        return;

    uint16_t spread = (uint16_t)(st->center_hi[ch] - st->center_lo[ch]);
    uint16_t mean = (uint16_t)(st->center_sum[ch] / st->center_count[ch]);

    /* Three guards, and they catch different things. `steady` rejects a stick
     * that moved while it was being measured. `near` rejects one that was held
     * still in the wrong place, which `steady` cannot see. `inside` is the
     * backstop that keeps rcl_config_validate()'s invariant -- the centre
     * strictly inside the endpoints -- true whatever the readings were. */
    int32_t shift = (int32_t)mean - (int32_t)cal->center_us;
    if (shift < 0)
        shift = -shift;

    bool steady = spread <= RCL_CENTER_SPREAD_US;
    bool near = shift <= RCL_CENTER_MAX_SHIFT_US;
    bool inside = mean > (uint16_t)(cal->min_us + 50) && mean < (uint16_t)(cal->max_us - 50);

    if (steady && near && inside)
        cal->center_us = mean;

    st->center_done[ch] = true;
}

/**
 * @brief Take in the new measurements and decide whether the link is up.
 * @param st State.
 * @param in Measurements, possibly NULL.
 * @param now_ms Current clock.
 */
static void step_inputs(rcl_state_t *st, const rcl_input_t *in, uint32_t now_ms)
{
    const rcl_config_t *c = &st->cfg;

    for (int i = 0; i < RCL_CH_COUNT; i++) {
        bool got = in && in->fresh[i] && in->pulse_us[i] >= c->pulse_min_us &&
                   in->pulse_us[i] <= c->pulse_max_us;
        if (got) {
            if (i < 2 && !st->center_done[i])
                step_center(st, i, in->pulse_us[i], now_ms);

            st->norm[i] = rcl_normalize(&c->cal[i], in->pulse_us[i]);
            st->last_seen_ms[i] = now_ms;
            st->chan_valid[i] = true;
        } else if (now_ms - st->last_seen_ms[i] >= c->signal_timeout_ms) {
            st->chan_valid[i] = false;
            st->norm[i] = 0;
        }

        /* Until a channel has been centred, report it as centred. Acting on a
         * stick whose rest point is still being measured is how a car flashes
         * an indicator or shows reverse in the first fraction of a second
         * after the receiver binds. */
        if (i < 2 && !st->center_done[i])
            st->norm[i] = 0;
    }

    /* Channel 3 only counts when it is configured: a two-channel receiver must
     * not sit in permanent failsafe. */
    st->failsafe = !st->chan_valid[RCL_CH_STEER] || !st->chan_valid[RCL_CH_THROTTLE] ||
                   (c->aux_mode != RCL_AUX_MODE_OFF && !st->chan_valid[RCL_CH_AUX]);
}

/**
 * @brief Advance the blink phase.
 *
 * Also the place where a turn signal that has been asked to stop actually
 * stops, so that it always ends on a completed cycle rather than mid-flash.
 *
 * @param st State.
 * @param dt Elapsed time, ms.
 */
static void step_blink(rcl_state_t *st, uint32_t dt)
{
    const rcl_config_t *c = &st->cfg;

    if (!st->hazard && st->turn == RCL_TURN_NONE) {
        st->blink_ms = 0;
        st->blink_on = false;
        return;
    }

    st->blink_ms += dt;
    if (st->blink_ms >= c->blink_period_ms) {
        st->blink_ms %= c->blink_period_ms;
        if (st->turn_ending) {
            st->turn = RCL_TURN_NONE;
            st->turn_ending = false;
            st->turn_ms = 0;
        }
    }

    uint32_t on_ms = (c->blink_period_ms * c->blink_duty) / 100u;
    st->blink_on = st->blink_ms < on_ms;
}

/**
 * @brief Restart the blink cycle at the beginning of its lit half.
 * @param st State.
 */
static void blink_restart(rcl_state_t *st)
{
    st->blink_ms = 0;
    st->blink_on = true;
}

/**
 * @brief Decide the drive state from the throttle.
 * @param st State.
 * @param dt Elapsed time, ms.
 */
static void step_drive(rcl_state_t *st, uint32_t dt)
{
    const rcl_config_t *c = &st->cfg;
    int16_t thr = st->norm[RCL_CH_THROTTLE];

    if (thr >= c->throttle_center_band) {
        st->drive = RCL_DRIVE_FORWARD;
        st->coast_left = c->coast_ms;
        st->back_held_ms = 0;
        /* Reverse has to be asked for again after driving forwards. */
        st->reverse_armed = false;
    } else if (thr <= -c->brake_threshold) {
        st->back_held_ms += dt;

        bool rolling = st->coast_left > 0u;
        bool may_reverse =
            !rolling && (c->esc_mode == RCL_ESC_DIRECT_REVERSE || st->reverse_armed);

        if (may_reverse) {
            /* Idle, not brake, while the debounce runs: showing the brake light
             * for a quarter second every time reverse is selected from a
             * standstill would be a flicker, not information. */
            st->drive = st->back_held_ms >= c->reverse_arm_ms ? RCL_DRIVE_REVERSE
                                                              : RCL_DRIVE_IDLE;
        } else {
            st->drive = RCL_DRIVE_BRAKE;
            st->coast_left = countdown(st->coast_left, dt);
            st->reverse_armed = false;
        }
    } else {
        st->back_held_ms = 0;
        st->coast_left = countdown(st->coast_left, dt);
        if (magnitude(thr) < c->throttle_center_band)
            st->reverse_armed = true;
        st->drive = RCL_DRIVE_IDLE;
    }

    if (st->drive == RCL_DRIVE_BRAKE)
        st->brake_left = c->brake_extend_ms;
    else
        st->brake_left = countdown(st->brake_left, dt);
}

/**
 * @brief Read channel 3 and apply the action its position selects.
 * @param st State.
 */
static void step_aux(rcl_state_t *st)
{
    const rcl_config_t *c = &st->cfg;

    st->hazard = false;
    st->aux_on = false;
    st->lights_off = false;

    if (c->aux_mode == RCL_AUX_MODE_OFF)
        return;

    int16_t a = st->norm[RCL_CH_AUX];
    if (a >= c->aux_high)
        st->aux_pos = 2;
    else if (a <= c->aux_low)
        st->aux_pos = 0;
    else if (c->aux_mode == RCL_AUX_MODE_3POS)
        st->aux_pos = 1;
    /* Two-position switch, reading between the thresholds: keep the last
     * position. The gap is hysteresis, not a third state. */

    switch (c->aux_action[st->aux_pos]) {
    case RCL_AUX_ACTION_HAZARD:
        st->hazard = true;
        break;
    case RCL_AUX_ACTION_AUX:
        st->aux_on = true;
        break;
    case RCL_AUX_ACTION_LIGHTS_OFF:
        st->lights_off = true;
        break;
    case RCL_AUX_ACTION_NONE:
    case RCL_AUX_ACTION_COUNT:
    default:
        break;
    }
}

/**
 * @brief Decide whether a turn signal should be running, and on which side.
 * @param st State.
 * @param dt Elapsed time, ms.
 */
static void step_turn(rcl_state_t *st, uint32_t dt)
{
    const rcl_config_t *c = &st->cfg;
    int16_t s = st->norm[RCL_CH_STEER];
    int16_t mag = magnitude(s);

    if (mag <= c->steer_center_band) {
        st->centered_ms += dt;
        if (st->centered_ms >= c->center_hold_ms)
            st->turn_armed = true;
    } else {
        st->centered_ms = 0;
    }

    rcl_turn_t side = s > 0 ? RCL_TURN_RIGHT : RCL_TURN_LEFT;

    if (st->turn == RCL_TURN_NONE) {
        /* Armed means: the steering sat still long enough that this deflection
         * is a deliberate turn and not a line correction. Arming is consumed
         * here, so the next signal needs the wheel centred again. */
        if (st->turn_armed && mag >= c->steer_trigger) {
            st->turn = side;
            st->turn_armed = false;
            st->turn_ms = 0;
            st->turn_ending = false;
            blink_restart(st);
        }
        return;
    }

    st->turn_ms += dt;

    if (mag >= c->steer_trigger && side != st->turn) {
        /* Steering straight from one lock to the other: switch sides at once
         * rather than finishing the old signal first. */
        st->turn = side;
        st->turn_ms = 0;
        st->turn_ending = false;
        blink_restart(st);
        return;
    }

    bool hold = mag >= c->steer_release || st->turn_ms < c->turn_min_ms;
    st->turn_ending = !hold;
}

/**
 * @brief Work out where every output should be.
 * @param st State.
 */
static void step_targets(rcl_state_t *st)
{
    const rcl_config_t *c = &st->cfg;
    bool park = c->park_lights_on && !st->lights_off;

    uint8_t front = park ? c->level_front_park : 0;
    if (!st->lights_off && (st->drive == RCL_DRIVE_FORWARD || st->drive == RCL_DRIVE_REVERSE))
        front = c->level_front_drive;

    /* The brake light is not switched off by the light switch. Neither is it on
     * a real car, and for the same reason. */
    uint8_t rear = park ? c->level_rear_park : 0;
    if (st->drive == RCL_DRIVE_BRAKE || st->brake_left > 0u)
        rear = c->level_rear_brake;

    uint8_t flash = st->blink_on ? c->level_turn : 0;

    st->target[RCL_OUT_FRONT] = front;
    st->target[RCL_OUT_REAR] = rear;
    st->target[RCL_OUT_REVERSE] = st->drive == RCL_DRIVE_REVERSE ? c->level_reverse : 0;
    st->target[RCL_OUT_TURN_LEFT] =
        st->hazard || st->turn == RCL_TURN_LEFT ? flash : 0;
    st->target[RCL_OUT_TURN_RIGHT] =
        st->hazard || st->turn == RCL_TURN_RIGHT ? flash : 0;
    st->target[RCL_OUT_AUX] = st->aux_on ? c->level_aux : 0;
}

/**
 * @brief Move the outputs towards their targets.
 *
 * Only the front and rear LED are slewed; everything else is a lamp that is
 * either on or off, and a fading indicator would read as a fault.
 *
 * @param st State.
 * @param dt Elapsed time, ms.
 */
static void step_fade(rcl_state_t *st, uint32_t dt)
{
    const rcl_config_t *c = &st->cfg;

    for (int i = 0; i < RCL_OUT_COUNT; i++) {
        bool fades = i == RCL_OUT_FRONT || i == RCL_OUT_REAR;
        if (!fades || c->fade_step == 0u) {
            st->out[i] = st->target[i];
            continue;
        }

        uint32_t step = ((uint32_t)c->fade_step * dt) / 10u;
        if (step == 0u) {
            /* Called again within the same few milliseconds; nothing to do
             * yet. Rounding up here instead would make the fade time depend on
             * how often the sketch happens to call loop(). */
            continue;
        }
        if (step > RCL_LEVEL_MAX)
            step = RCL_LEVEL_MAX;

        int32_t cur = st->out[i];
        int32_t want = st->target[i];
        if (want > cur)
            cur += (int32_t)step > want - cur ? want - cur : (int32_t)step;
        else if (want < cur)
            cur -= (int32_t)step > cur - want ? cur - want : (int32_t)step;
        st->out[i] = (uint8_t)cur;
    }
}

/**
 * @brief Everything off, or the hazards blinking, while the link is down.
 * @param st State.
 * @param dt Elapsed time, ms.
 */
static void step_failsafe(rcl_state_t *st, uint32_t dt)
{
    st->drive = RCL_DRIVE_IDLE;
    st->coast_left = 0;
    st->brake_left = 0;
    st->back_held_ms = 0;
    st->reverse_armed = false;

    st->turn = RCL_TURN_NONE;
    st->turn_ending = false;
    st->turn_ms = 0;
    st->centered_ms = 0;
    st->turn_armed = false;

    st->aux_on = false;
    st->lights_off = true;
    st->hazard = st->cfg.failsafe_hazard;

    step_blink(st, dt);

    for (int i = 0; i < RCL_OUT_COUNT; i++)
        st->target[i] = 0;
    if (st->hazard) {
        uint8_t flash = st->blink_on ? st->cfg.level_turn : 0;
        st->target[RCL_OUT_TURN_LEFT] = flash;
        st->target[RCL_OUT_TURN_RIGHT] = flash;
    }
}

void rcl_update(rcl_state_t *st, const rcl_input_t *in, uint32_t now_ms)
{
    if (!st || !st->started)
        return;

    /* Unsigned difference, so a clock that has wrapped past 2^32 needs no
     * special case. The clamp covers the other direction: a sketch that was
     * blocked for a second should not fast-forward the blink phase by a
     * second's worth of cycles. */
    uint32_t dt = now_ms - st->last_ms;
    if (dt > RCL_DT_MAX_MS)
        dt = RCL_DT_MAX_MS;
    st->last_ms = now_ms;

    step_inputs(st, in, now_ms);

    if (st->failsafe) {
        step_failsafe(st, dt);
        step_fade(st, dt);
        return;
    }

    /* Order matters: the aux switch can raise the hazards, which step_blink()
     * has to see in the same update, and step_turn() has to run before it too
     * so that a signal starting this update begins lit. */
    step_drive(st, dt);
    step_aux(st);
    step_turn(st, dt);
    step_blink(st, dt);
    step_targets(st);
    step_fade(st, dt);
}

uint8_t rcl_output(const rcl_state_t *st, rcl_output_t out)
{
    if (!st || (int)out < 0 || out >= RCL_OUT_COUNT)
        return 0;
    return st->out[out];
}

int16_t rcl_channel(const rcl_state_t *st, rcl_channel_t ch)
{
    if (!st || (int)ch < 0 || ch >= RCL_CH_COUNT)
        return 0;
    return st->norm[ch];
}

bool rcl_centering(const rcl_state_t *st)
{
    if (!st)
        return false;
    return !st->center_done[RCL_CH_STEER] || !st->center_done[RCL_CH_THROTTLE];
}

const char *rcl_drive_name(rcl_drive_t drive)
{
    switch (drive) {
    case RCL_DRIVE_IDLE:
        return "idle";
    case RCL_DRIVE_FORWARD:
        return "forward";
    case RCL_DRIVE_BRAKE:
        return "brake";
    case RCL_DRIVE_REVERSE:
        return "reverse";
    default:
        return "?";
    }
}
