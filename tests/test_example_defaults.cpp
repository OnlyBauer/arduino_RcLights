/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file test_example_defaults.cpp
 * @brief Checks that the RcLightsCar example's OPTIONAL block still lists the
 *        library's own defaults.
 *
 * That block exists to show a beginner every setting there is, next to the
 * value the car already uses, so that deleting the whole block changes nothing
 * and keeping one line changes exactly that one thing. The moment a default
 * moves in `rcl_config_default()` and the block does not follow, it stops being
 * documentation and starts being a set of silent overrides — the example would
 * pin the old value while the library had moved on, and nobody would notice
 * because everything still compiles and still drives.
 *
 * So: run the example's own `setup()`, and require what the controller ends up
 * with to be byte-for-byte what `rcl_config_default()` produces, give or take
 * the three settings the example is supposed to change.
 *
 * The example is compiled here as it ships, `.ino` extension and all; it needs
 * nothing from the Arduino runtime that `arduino_stubs/` does not provide, and
 * it uses no `Serial`.
 */

#include <Arduino.h>

#include "RcLights.h"
#include "test_util.h"

/* The sketch, verbatim. It brings its own `lights` object, `setup()` and
 * `loop()`, and its OUTPUTS / ESC_MODE / CH3_MODE macros. */
#include "RcLightsCar.ino"

/**
 * @brief Run the example's setup() and compare against the library defaults.
 */
static void test_optional_block_matches_defaults(void)
{
    rcl_config_t want;
    rcl_config_default(&want);

    /* The three the example is meant to set. Everything else it writes, it
     * writes to the value it already had. */
    want.esc_mode = ESC_MODE;
    want.aux_mode = CH3_MODE;

    mock_reset();
    setup();

    const rcl_config_t &got = lights.config();

    /* Field by field rather than memcmp: a structure with padding has no unique
     * object representation, so comparing its bytes is not something to build a
     * test on -- and a byte offset is not a useful thing to report anyway.
     *
     * Which leaves the question of a field nobody listed here. That is what the
     * size check is for: add a setting to rcl_config_t and this fails, and
     * whoever added it has to decide whether the example should mention it. It
     * is a tripwire, not a measurement, so a change in padding or in a field's
     * width tripping it is the system working. */
    CHECK_EQ_INT(sizeof(rcl_config_t), 116,
                 "rcl_config_t has not gained a setting this test does not know of");

    CHECK_EQ_INT(got.level_front_park, want.level_front_park, "level_front_park");
    CHECK_EQ_INT(got.level_front_drive, want.level_front_drive, "level_front_drive");
    CHECK_EQ_INT(got.level_rear_park, want.level_rear_park, "level_rear_park");
    CHECK_EQ_INT(got.level_rear_brake, want.level_rear_brake, "level_rear_brake");
    CHECK_EQ_INT(got.level_reverse, want.level_reverse, "level_reverse");
    CHECK_EQ_INT(got.level_turn, want.level_turn, "level_turn");
    CHECK_EQ_INT(got.level_aux, want.level_aux, "level_aux");
    CHECK_EQ_INT(got.fade_step, want.fade_step, "fade_step");
    CHECK_EQ_INT(got.park_lights_on, want.park_lights_on, "park_lights_on");
    CHECK_EQ_INT(got.failsafe_hazard, want.failsafe_hazard, "failsafe_hazard");
    CHECK_EQ_INT(got.steer_trigger, want.steer_trigger, "steer_trigger");
    CHECK_EQ_INT(got.steer_release, want.steer_release, "steer_release");
    CHECK_EQ_INT(got.steer_center_band, want.steer_center_band, "steer_center_band");
    CHECK_EQ_INT(got.center_hold_ms, want.center_hold_ms, "center_hold_ms");
    CHECK_EQ_INT(got.turn_min_ms, want.turn_min_ms, "turn_min_ms");
    CHECK_EQ_INT(got.blink_period_ms, want.blink_period_ms, "blink_period_ms");
    CHECK_EQ_INT(got.blink_duty, want.blink_duty, "blink_duty");
    CHECK_EQ_INT(got.coast_ms, want.coast_ms, "coast_ms");
    CHECK_EQ_INT(got.brake_threshold, want.brake_threshold, "brake_threshold");
    CHECK_EQ_INT(got.throttle_center_band, want.throttle_center_band,
                 "throttle_center_band");
    CHECK_EQ_INT(got.reverse_arm_ms, want.reverse_arm_ms, "reverse_arm_ms");
    CHECK_EQ_INT(got.brake_extend_ms, want.brake_extend_ms, "brake_extend_ms");
    CHECK_EQ_INT(got.aux_action[0], want.aux_action[0], "aux_action low");
    CHECK_EQ_INT(got.aux_action[1], want.aux_action[1], "aux_action middle");
    CHECK_EQ_INT(got.aux_action[2], want.aux_action[2], "aux_action high");
    CHECK_EQ_INT(got.aux_low, want.aux_low, "aux_low");
    CHECK_EQ_INT(got.aux_high, want.aux_high, "aux_high");
    CHECK_EQ_INT(got.auto_center_ms, want.auto_center_ms, "auto_center_ms");
    CHECK_EQ_INT(got.signal_timeout_ms, want.signal_timeout_ms, "signal_timeout_ms");
    CHECK_EQ_INT(got.pulse_min_us, want.pulse_min_us, "pulse_min_us");
    CHECK_EQ_INT(got.pulse_max_us, want.pulse_max_us, "pulse_max_us");

    for (int i = 0; i < RCL_CH_COUNT; i++) {
        CHECK_EQ_INT(got.cal[i].min_us, want.cal[i].min_us, "cal min_us");
        CHECK_EQ_INT(got.cal[i].max_us, want.cal[i].max_us, "cal max_us");
        CHECK_EQ_INT(got.cal[i].invert, want.cal[i].invert, "cal invert");
    }
}

/**
 * @brief The example starts: its settings are ones the library will accept.
 */
static void test_example_starts(void)
{
    mock_reset();
    setup();
    CHECK(lights.config().esc_mode == ESC_MODE, "the example's ESC_MODE is in force");
    CHECK(lights.config().aux_mode == CH3_MODE, "and its CH3_MODE");
    loop(); /* must not crash with no signal present */
    lights.end();
}

/**
 * @brief Run every case.
 * @return 0 when they all passed.
 */
int main(void)
{
    test_begin("example");

    test_optional_block_matches_defaults();
    test_example_starts();

    return test_end();
}
