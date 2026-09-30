/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file test_core.c
 * @brief Host tests for the controller in rclights_core.c.
 *
 * Each case is a sequence of stick positions and a clock, stepped in 10 ms
 * slices so that a threshold needing an unrealistic update rate shows up here.
 * Pulse widths are microseconds, so a case reads the way it would be flown.
 */

#include "rclights_core.h"
#include "test_util.h"

/** @brief Centre stick, in microseconds. */
#define US_CENTER 1500
/** @brief Well past the turn trigger, one way. */
#define US_RIGHT 1900
/** @brief Well past the turn trigger, the other way. */
#define US_LEFT 1100
/** @brief Clear forward throttle. */
#define US_FORWARD 1800
/** @brief Clear backward throttle. */
#define US_BACK 1200
/** @brief Three-position switch, low. */
#define US_SW_LOW 1000
/** @brief Three-position switch, middle. */
#define US_SW_MID 1500
/** @brief Three-position switch, high. */
#define US_SW_HIGH 2000

/** @brief Size of one simulated time slice, in milliseconds. */
#define SLICE_MS 10

/** @brief The clock the cases share, advanced only by run() and starve(). */
static uint32_t g_now;

/**
 * @brief Feed the controller a steady stick position for a while.
 * @param st State.
 * @param steer_us Steering pulse.
 * @param thr_us Throttle pulse.
 * @param aux_us Channel 3 pulse.
 * @param ms How long to hold it.
 */
static void run(rcl_state_t *st, uint16_t steer_us, uint16_t thr_us, uint16_t aux_us,
                uint32_t ms)
{
    rcl_input_t in;
    in.pulse_us[RCL_CH_STEER] = steer_us;
    in.pulse_us[RCL_CH_THROTTLE] = thr_us;
    in.pulse_us[RCL_CH_AUX] = aux_us;
    for (int i = 0; i < RCL_CH_COUNT; i++)
        in.fresh[i] = true;

    for (uint32_t t = 0; t < ms; t += SLICE_MS) {
        g_now += SLICE_MS;
        rcl_update(st, &in, g_now);
    }
}

/**
 * @brief Let the link go quiet for a while.
 * @param st State.
 * @param ms How long.
 */
static void starve(rcl_state_t *st, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += SLICE_MS) {
        g_now += SLICE_MS;
        rcl_update(st, NULL, g_now);
    }
}

/**
 * @brief Start a controller with the defaults and a link that is already up.
 * @param st State to start.
 * @param cfg Configuration, or NULL for the defaults.
 */
static void boot(rcl_state_t *st, const rcl_config_t *cfg)
{
    g_now = 100000; /* Not zero, so a wrongly initialised timer shows up. */
    CHECK(rcl_init(st, cfg, g_now), "init succeeds");
    CHECK(st->failsafe, "starts in failsafe, before any pulse");
    run(st, US_CENTER, US_CENTER, US_SW_LOW, 200);
    CHECK(!st->failsafe, "link is up once pulses arrive");
}

/** @brief Normalisation, including the asymmetric-endpoint case. */
static void test_normalize(void)
{
    rcl_cal_t cal = {1000, 1500, 2000, false};

    CHECK_EQ_INT(rcl_normalize(&cal, 1500), 0, "centre reads zero");
    CHECK_EQ_INT(rcl_normalize(&cal, 2000), RCL_UNIT, "top endpoint reads full");
    CHECK_EQ_INT(rcl_normalize(&cal, 1000), -RCL_UNIT, "bottom endpoint reads full");
    CHECK_EQ_INT(rcl_normalize(&cal, 1750), 500, "half way up");
    CHECK_EQ_INT(rcl_normalize(&cal, 2400), RCL_UNIT, "overshoot is clamped");
    CHECK_EQ_INT(rcl_normalize(&cal, 600), -RCL_UNIT, "undershoot is clamped");

    /* An off-centre trim must move the rest point without skewing the stops:
     * both halves still reach exactly full scale. */
    cal.center_us = 1600;
    CHECK_EQ_INT(rcl_normalize(&cal, 1600), 0, "trimmed centre reads zero");
    CHECK_EQ_INT(rcl_normalize(&cal, 2000), RCL_UNIT, "trimmed top still full");
    CHECK_EQ_INT(rcl_normalize(&cal, 1000), -RCL_UNIT, "trimmed bottom still full");

    cal.center_us = 1500;
    cal.invert = true;
    CHECK_EQ_INT(rcl_normalize(&cal, 2000), -RCL_UNIT, "invert mirrors the result");

    CHECK_EQ_INT(rcl_normalize(NULL, 1500), 0, "NULL calibration reads zero");
}

/** @brief The configuration checks that keep nonsense out of the core. */
static void test_config(void)
{
    rcl_config_t cfg;
    rcl_config_default(&cfg);
    CHECK(rcl_config_validate(&cfg), "the defaults are valid");
    CHECK(!rcl_config_validate(NULL), "NULL is not");

    rcl_config_t bad = cfg;
    bad.cal[RCL_CH_STEER].center_us = 900; /* below its own minimum */
    CHECK(!rcl_config_validate(&bad), "centre outside the endpoints is refused");

    bad = cfg;
    bad.steer_release = bad.steer_trigger + 10;
    CHECK(!rcl_config_validate(&bad), "release above trigger is refused");

    bad = cfg;
    bad.steer_release = bad.steer_center_band;
    CHECK(!rcl_config_validate(&bad), "release inside the centre band is refused");

    bad = cfg;
    bad.blink_period_ms = 0;
    CHECK(!rcl_config_validate(&bad), "a zero blink period is refused");

    bad = cfg;
    bad.blink_duty = 100;
    CHECK(!rcl_config_validate(&bad), "a 100 % duty cycle is refused");

    bad = cfg;
    bad.aux_low = bad.aux_high;
    CHECK(!rcl_config_validate(&bad), "aux thresholds that cross are refused");

    rcl_state_t st;
    bad = cfg;
    bad.blink_period_ms = 0;
    CHECK(!rcl_init(&st, &bad, 0), "init refuses an invalid configuration");
}

/** @brief Park lights, and the front light brightening while driving. */
static void test_park_and_drive(void)
{
    rcl_state_t st;
    boot(&st, NULL);

    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_FRONT), 40, "front sits at park level");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 30, "rear sits at park level");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REVERSE), 0, "reversing light is off");
    CHECK_EQ_INT(st.drive, RCL_DRIVE_IDLE, "standing still");

    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 300);
    CHECK_EQ_INT(st.drive, RCL_DRIVE_FORWARD, "throttle forward is forward");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_FRONT), 255, "front brightens when driving");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 30, "rear stays at park level");

    /* Back to neutral: the front light drops to park again, and the fade is
     * what makes it take more than one update to get there. */
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 20);
    CHECK(rcl_output(&st, RCL_OUT_FRONT) > 40, "front fades rather than snapping");
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 200);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_FRONT), 40, "front is back at park level");
}

/** @brief Braking, and the hold that keeps the brake light on a moment longer. */
static void test_brake(void)
{
    rcl_state_t st;
    boot(&st, NULL);

    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 500);
    run(&st, US_CENTER, US_BACK, US_SW_LOW, 100);
    CHECK_EQ_INT(st.drive, RCL_DRIVE_BRAKE, "backwards while rolling is braking");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 255, "brake light at full");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REVERSE), 0,
                 "no reversing light while braking");

    /* Released: the light holds for brake_extend_ms (400 ms) and then falls
     * back to the park level. */
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 200);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 255, "brake light holds briefly");
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 400);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 30, "and then returns to park");
}

/** @brief Reverse needs a pass through neutral on a car-style ESC. */
static void test_reverse_needs_neutral(void)
{
    rcl_state_t st;
    boot(&st, NULL);

    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 500);
    /* Held back without letting go: coast runs out, but this is a car standing
     * on its brakes, not reversing. */
    run(&st, US_CENTER, US_BACK, US_SW_LOW, 3000);
    CHECK_EQ_INT(st.drive, RCL_DRIVE_BRAKE, "held brake never becomes reverse");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REVERSE), 0, "reversing light stays off");

    /* Neutral, then back again: now it is reverse. */
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 300);
    run(&st, US_CENTER, US_BACK, US_SW_LOW, 100);
    CHECK_EQ_INT(st.drive, RCL_DRIVE_IDLE, "the arming delay has not run out yet");
    run(&st, US_CENTER, US_BACK, US_SW_LOW, 300);
    CHECK_EQ_INT(st.drive, RCL_DRIVE_REVERSE, "back after neutral is reverse");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REVERSE), 255, "reversing light on");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_FRONT), 255,
                 "front is at driving level in reverse too");
}

/** @brief On a direct-reverse ESC the same stick reverses without neutral. */
static void test_reverse_direct(void)
{
    rcl_config_t cfg;
    rcl_config_default(&cfg);
    cfg.esc_mode = RCL_ESC_DIRECT_REVERSE;

    rcl_state_t st;
    boot(&st, &cfg);

    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 500);
    run(&st, US_CENTER, US_BACK, US_SW_LOW, 300);
    CHECK_EQ_INT(st.drive, RCL_DRIVE_BRAKE, "still braking while it rolls out");

    run(&st, US_CENTER, US_BACK, US_SW_LOW, 2000);
    CHECK_EQ_INT(st.drive, RCL_DRIVE_REVERSE, "and then reverses, without neutral");
}

/** @brief A turn signal needs the wheel to have been still first. */
static void test_turn_arming(void)
{
    rcl_state_t st;
    rcl_config_t cfg;
    rcl_config_default(&cfg);

    /* Steering before the centre hold has elapsed: the mid-corner correction
     * the feature exists to ignore. */
    g_now = 1000;
    CHECK(rcl_init(&st, &cfg, g_now), "init");
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 200);
    run(&st, US_RIGHT, US_CENTER, US_SW_LOW, 200);
    CHECK_EQ_INT(st.turn, RCL_TURN_NONE, "an early deflection does not indicate");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_RIGHT), 0, "and nothing blinks");

    /* Case two: the same deflection after the wheel has been still. */
    boot(&st, &cfg);
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 500);
    run(&st, US_RIGHT, US_CENTER, US_SW_LOW, 20);
    CHECK_EQ_INT(st.turn, RCL_TURN_RIGHT, "a deliberate turn indicates");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_RIGHT), 255, "starting on the lit half");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_LEFT), 0, "only the one side");

    /* It blinks: half a period later the lamp is out again. */
    run(&st, US_RIGHT, US_CENTER, US_SW_LOW, 360);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_RIGHT), 0, "and it is a blink");
    run(&st, US_RIGHT, US_CENTER, US_SW_LOW, 360);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_RIGHT), 255, "lit again next cycle");

    /* Straightening up ends it, but only after the minimum running time and
     * only at the end of a cycle. */
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 100);
    CHECK_EQ_INT(st.turn, RCL_TURN_RIGHT, "still running: below the minimum time");
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 1500);
    CHECK_EQ_INT(st.turn, RCL_TURN_NONE, "and then it stops");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_RIGHT), 0, "lamp out");

    /* The 1500 ms of straight running above re-armed it, so the other side
     * indicates immediately. */
    run(&st, US_LEFT, US_CENTER, US_SW_LOW, 50);
    CHECK_EQ_INT(st.turn, RCL_TURN_LEFT, "the other side indicates when armed");
}

/** @brief Steering from one lock to the other swaps sides at once. */
static void test_turn_side_change(void)
{
    rcl_state_t st;
    boot(&st, NULL);

    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 500);
    run(&st, US_RIGHT, US_CENTER, US_SW_LOW, 100);
    CHECK_EQ_INT(st.turn, RCL_TURN_RIGHT, "right first");

    run(&st, US_LEFT, US_CENTER, US_SW_LOW, 20);
    CHECK_EQ_INT(st.turn, RCL_TURN_LEFT, "and left immediately, without re-arming");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_LEFT), 255, "on the lit half again");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_RIGHT), 0, "the old side is out");
}

/** @brief Channel 3 in its three-position form: nothing, aux, hazards. */
static void test_aux_three_position(void)
{
    rcl_state_t st;
    boot(&st, NULL); /* defaults: low = none, middle = aux, high = hazard */

    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_AUX), 0, "low position does nothing");

    run(&st, US_CENTER, US_CENTER, US_SW_MID, 50);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_AUX), 255, "middle switches the aux output");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_LEFT), 0, "and nothing blinks");

    run(&st, US_CENTER, US_CENTER, US_SW_HIGH, 20);
    CHECK(st.hazard, "high position raises the hazards");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_LEFT), 255, "both sides lit");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_RIGHT), 255, "both sides lit");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_AUX), 0, "aux is back off");

    run(&st, US_CENTER, US_CENTER, US_SW_HIGH, 360);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_LEFT), 0, "and they blink");

    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 50);
    CHECK(!st.hazard, "back to low, hazards off");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_TURN_LEFT), 0, "lamps out");
}

/** @brief Channel 3 as a plain on/off switch driving the hazards. */
static void test_aux_two_position(void)
{
    rcl_config_t cfg;
    rcl_config_default(&cfg);
    cfg.aux_mode = RCL_AUX_MODE_2POS;
    cfg.aux_action[0] = RCL_AUX_ACTION_NONE;
    cfg.aux_action[2] = RCL_AUX_ACTION_HAZARD;

    rcl_state_t st;
    boot(&st, &cfg);
    CHECK(!st.hazard, "switch down: nothing");

    run(&st, US_CENTER, US_CENTER, US_SW_HIGH, 20);
    CHECK(st.hazard, "switch up: hazards");

    /* The gap between the two thresholds is hysteresis, not a third position:
     * a signal that drifts into it keeps the last position rather than
     * flickering. */
    run(&st, US_CENTER, US_CENTER, US_SW_MID, 20);
    CHECK(st.hazard, "a reading between the thresholds holds the last position");

    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 20);
    CHECK(!st.hazard, "and the far side switches it off again");
}

/** @brief The light switch leaves the brake and the turn signals alone. */
static void test_lights_off_action(void)
{
    rcl_config_t cfg;
    rcl_config_default(&cfg);
    cfg.aux_action[1] = RCL_AUX_ACTION_LIGHTS_OFF;

    rcl_state_t st;
    boot(&st, &cfg);
    run(&st, US_CENTER, US_CENTER, US_SW_MID, 200);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_FRONT), 0, "park light off");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 0, "tail light off");

    run(&st, US_CENTER, US_FORWARD, US_SW_MID, 300);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_FRONT), 0,
                 "and it stays off while driving");

    run(&st, US_CENTER, US_BACK, US_SW_MID, 100);
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 255,
                 "but the brake light still works");
}

/** @brief Losing the link. */
static void test_failsafe(void)
{
    rcl_state_t st;
    boot(&st, NULL);
    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 300);

    starve(&st, 300);
    CHECK(!st.failsafe, "a short gap is not a failure");

    starve(&st, 400);
    CHECK(st.failsafe, "but silence past the timeout is");
    CHECK_EQ_INT(st.drive, RCL_DRIVE_IDLE, "the drive state is dropped");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_FRONT), 0, "front light out");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 0, "rear light out");
    CHECK(rcl_output(&st, RCL_OUT_TURN_LEFT) == rcl_output(&st, RCL_OUT_TURN_RIGHT),
          "the hazards blink together");

    /* And recovers on its own. */
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 100);
    CHECK(!st.failsafe, "the link coming back clears it");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_REAR), 30, "park lights return");
}

/** @brief A two-channel receiver is not a fault. */
static void test_two_channel_receiver(void)
{
    rcl_config_t cfg;
    rcl_config_default(&cfg);
    cfg.aux_mode = RCL_AUX_MODE_OFF;

    rcl_state_t st;
    g_now = 5000;
    CHECK(rcl_init(&st, &cfg, g_now), "init");

    rcl_input_t in;
    in.pulse_us[RCL_CH_STEER] = US_CENTER;
    in.pulse_us[RCL_CH_THROTTLE] = US_CENTER;
    in.pulse_us[RCL_CH_AUX] = 0;
    in.fresh[RCL_CH_STEER] = true;
    in.fresh[RCL_CH_THROTTLE] = true;
    in.fresh[RCL_CH_AUX] = false; /* nothing wired to channel 3 at all */

    for (int i = 0; i < 100; i++) {
        g_now += SLICE_MS;
        rcl_update(&st, &in, g_now);
    }

    CHECK(!st.failsafe, "a missing channel 3 does not trip the failsafe");
    CHECK_EQ_INT(rcl_output(&st, RCL_OUT_FRONT), 40, "park lights are on");
}

/** @brief Implausible pulses are discarded rather than acted on. */
static void test_pulse_filter(void)
{
    rcl_state_t st;
    boot(&st, NULL);

    rcl_input_t in;
    in.pulse_us[RCL_CH_STEER] = 300; /* far below pulse_min_us */
    in.pulse_us[RCL_CH_THROTTLE] = US_CENTER;
    in.pulse_us[RCL_CH_AUX] = US_SW_LOW;
    for (int i = 0; i < RCL_CH_COUNT; i++)
        in.fresh[i] = true;

    for (int i = 0; i < 100; i++) { /* a full second of nonsense on channel 1 */
        g_now += SLICE_MS;
        rcl_update(&st, &in, g_now);
    }

    CHECK(st.failsafe, "a channel sending only rubbish counts as lost");
    CHECK_EQ_INT(rcl_channel(&st, RCL_CH_STEER), 0, "and its value is not used");
}

/** @brief The clock wrapping past 2^32 changes nothing. */
static void test_clock_wrap(void)
{
    rcl_state_t st;
    rcl_config_t cfg;
    rcl_config_default(&cfg);

    g_now = 0xFFFFF000u;
    CHECK(rcl_init(&st, &cfg, g_now), "init just before the wrap");
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 200);
    CHECK(!st.failsafe, "link up");

    /* Step across 0xFFFFFFFF. */
    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 8000);
    CHECK(g_now < 0x00010000u, "the clock really did wrap");
    CHECK(!st.failsafe, "and nothing noticed");
    CHECK_EQ_INT(st.drive, RCL_DRIVE_FORWARD, "the drive state survived it");
}

/** @brief The centres are measured from the sticks at rest after boot. */
static void test_auto_center(void)
{
    rcl_state_t st;
    rcl_config_t cfg;
    rcl_config_default(&cfg);

    g_now = 2000;
    CHECK(rcl_init(&st, &cfg, g_now), "init");
    CHECK(rcl_centering(&st), "centring starts pending");

    /* A transmitter whose trims sit 60 µs off on both sticks. */
    run(&st, 1560, 1440, US_SW_LOW, 50);
    CHECK(rcl_centering(&st), "still measuring");
    CHECK_EQ_INT(rcl_channel(&st, RCL_CH_STEER), 0,
                 "and the stick reads centred while it does");

    run(&st, 1560, 1440, US_SW_LOW, 250);
    CHECK(!rcl_centering(&st), "the window closes");
    CHECK_EQ_INT(st.cfg.cal[RCL_CH_STEER].center_us, 1560, "steering centre taken");
    CHECK_EQ_INT(st.cfg.cal[RCL_CH_THROTTLE].center_us, 1440, "throttle centre taken");
    CHECK_EQ_INT(rcl_channel(&st, RCL_CH_STEER), 0, "rest now reads as zero");
    CHECK_EQ_INT(st.drive, RCL_DRIVE_IDLE, "and the car is idle, not creeping");

    /* And the endpoints still reach full scale from the moved centre. */
    run(&st, 2000, US_CENTER, US_SW_LOW, 50);
    CHECK_EQ_INT(rcl_channel(&st, RCL_CH_STEER), RCL_UNIT, "full lock still reads full");
}

/** @brief A stick that was being held at power-up is not taken as centre. */
static void test_auto_center_rejects_held_stick(void)
{
    rcl_state_t st;
    rcl_config_t cfg;
    rcl_config_default(&cfg);

    g_now = 3000;
    CHECK(rcl_init(&st, &cfg, g_now), "init");

    /* Throttle held forward, then released halfway through the window. The
     * readings spread far more than a stick at rest ever would. */
    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 100);
    run(&st, US_CENTER, US_CENTER, US_SW_LOW, 200);

    CHECK(!rcl_centering(&st), "the window still closes");
    CHECK_EQ_INT(st.cfg.cal[RCL_CH_THROTTLE].center_us, 1500,
                 "but the configured centre is kept");

    /* Which means the throttle works normally from here. */
    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 200);
    CHECK_EQ_INT(st.drive, RCL_DRIVE_FORWARD, "throttle behaves as configured");
}

/** @brief A stick held steadily in the wrong place is not taken as centre. */
static void test_auto_center_rejects_steady_offset(void)
{
    rcl_state_t st;
    rcl_config_t cfg;
    rcl_config_default(&cfg);

    g_now = 3500;
    CHECK(rcl_init(&st, &cfg, g_now), "init");

    /* Full throttle from the first pulse and never released. The readings do
     * not spread at all, so only the distance from the configured centre gives
     * it away. */
    run(&st, US_CENTER, US_FORWARD, US_SW_LOW, 300);

    CHECK(!rcl_centering(&st), "the window closes");
    CHECK_EQ_INT(st.cfg.cal[RCL_CH_THROTTLE].center_us, 1500,
                 "a centre that far out is refused");
    CHECK_EQ_INT(st.drive, RCL_DRIVE_FORWARD,
                 "so the throttle reads as the full throttle it is");
}

/** @brief Centring waits for the receiver rather than for the clock. */
static void test_auto_center_waits_for_signal(void)
{
    rcl_state_t st;
    rcl_config_t cfg;
    rcl_config_default(&cfg);

    g_now = 4000;
    CHECK(rcl_init(&st, &cfg, g_now), "init");

    /* The car is switched on before the transmitter. Nothing arrives for two
     * seconds, which is many times the window. */
    starve(&st, 2000);
    CHECK(rcl_centering(&st), "nothing was measured, because nothing arrived");

    run(&st, 1560, US_CENTER, US_SW_LOW, 300);
    CHECK(!rcl_centering(&st), "the window runs once pulses do arrive");
    CHECK_EQ_INT(st.cfg.cal[RCL_CH_STEER].center_us, 1560, "and the centre is taken");
}

/** @brief Setting the window to zero uses the configured centres as given. */
static void test_auto_center_off(void)
{
    rcl_state_t st;
    rcl_config_t cfg;
    rcl_config_default(&cfg);
    cfg.auto_center_ms = 0;

    g_now = 5000;
    CHECK(rcl_init(&st, &cfg, g_now), "init");
    CHECK(!rcl_centering(&st), "nothing to measure");

    run(&st, 1560, US_CENTER, US_SW_LOW, 50);
    CHECK_EQ_INT(st.cfg.cal[RCL_CH_STEER].center_us, 1500, "centre untouched");
    CHECK_EQ_INT(rcl_channel(&st, RCL_CH_STEER), 120, "and the stick is live at once");
}

/** @brief Arguments the core has to survive. */
static void test_guards(void)
{
    rcl_state_t st;
    CHECK(!rcl_init(NULL, NULL, 0), "init rejects a NULL state");

    rcl_update(NULL, NULL, 0); /* must not crash */

    boot(&st, NULL);
    CHECK_EQ_INT(rcl_output(&st, (rcl_output_t)99), 0, "a bad output reads zero");
    CHECK_EQ_INT(rcl_output(NULL, RCL_OUT_FRONT), 0, "a NULL state reads zero");
    CHECK_EQ_INT(rcl_channel(&st, (rcl_channel_t)99), 0, "a bad channel reads zero");

    rcl_config_default(NULL); /* must not crash */

    CHECK(rcl_drive_name(RCL_DRIVE_BRAKE)[0] == 'b', "drive states have names");
    CHECK(rcl_drive_name((rcl_drive_t)99)[0] == '?', "and a bad one is marked");
}

/**
 * @brief Run every case.
 * @return 0 when they all passed.
 */
int main(void)
{
    test_begin("core");

    test_normalize();
    test_config();
    test_park_and_drive();
    test_brake();
    test_reverse_needs_neutral();
    test_reverse_direct();
    test_turn_arming();
    test_turn_side_change();
    test_aux_three_position();
    test_aux_two_position();
    test_lights_off_action();
    test_failsafe();
    test_two_channel_receiver();
    test_pulse_filter();
    test_clock_wrap();
    test_auto_center();
    test_auto_center_rejects_held_stick();
    test_auto_center_rejects_steady_offset();
    test_auto_center_waits_for_signal();
    test_auto_center_off();
    test_guards();

    return test_end();
}
