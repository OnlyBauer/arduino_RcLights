/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file test_arduino_port.cpp
 * @brief Host tests for the Arduino wrapper in src/RcLights.cpp.
 *
 * The code under test is the real, shipping wrapper. What is replaced is
 * `Arduino.h`, by `arduino_stubs/` — so the clock, the pins and the interrupts
 * are things a test controls. Pulses are delivered the way a receiver delivers
 * them, as a rising edge, a wait and a falling edge, each one calling the
 * handler the wrapper installed.
 *
 * The core's behaviour is not re-tested here; `test_core.c` does that without a
 * board in the way. What this checks is the plumbing: that a pulse on a pin
 * arrives at the core with the width it had, that the core's output ends up on
 * the right pin, that inversion and the single-instance rule hold, and that
 * end() really lets go.
 */

#include <Arduino.h>

#include "RcLights.h"
#include "test_util.h"

namespace
{

/** @brief Steering input pin. */
const uint8_t PIN_CH1 = 2;
/** @brief Throttle input pin. */
const uint8_t PIN_CH2 = 4;
/** @brief Switch input pin. */
const uint8_t PIN_CH3 = 7;
/** @brief Front light output pin. */
const uint8_t PIN_FRONT = 3;
/** @brief Rear light output pin. */
const uint8_t PIN_REAR = 5;
/** @brief Reversing light output pin. */
const uint8_t PIN_REVERSE = 6;
/** @brief Left turn signal output pin. */
const uint8_t PIN_LEFT = 9;
/** @brief Right turn signal output pin. */
const uint8_t PIN_RIGHT = 10;
/** @brief Auxiliary output pin. */
const uint8_t PIN_AUX = 11;

/** @brief The pin map every case here uses. */
const RcLightsPins kPins = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_FRONT, PIN_REAR,
                            PIN_REVERSE, PIN_LEFT, PIN_RIGHT, PIN_AUX};

/**
 * @brief Send frames for a while, calling loop() in between as a sketch would.
 * @param lights The controller.
 * @param steer_us Steering pulse.
 * @param thr_us Throttle pulse.
 * @param aux_us Switch pulse.
 * @param ms Roughly how long to keep it up.
 */
void feed(RcLights &lights, uint32_t steer_us, uint32_t thr_us, uint32_t aux_us,
          uint32_t ms)
{
    uint32_t end = millis() + ms;
    while (millis() < end) {
        /* One frame: three pulses back to back, as a receiver puts them out. */
        mock_pulse(PIN_CH1, steer_us);
        mock_pulse(PIN_CH2, thr_us);
        mock_pulse(PIN_CH3, aux_us);
        lights.loop();
        /* The rest of the frame period, with the sketch still running. */
        for (int i = 0; i < 3; i++) {
            mock_advance_us(5000);
            lights.loop();
        }
    }
}

/**
 * @brief Run loop() for a while without any pulses arriving.
 * @param lights The controller.
 * @param ms How long.
 */
void quiet(RcLights &lights, uint32_t ms)
{
    uint32_t end = millis() + ms;
    while (millis() < end) {
        mock_advance_us(5000);
        lights.loop();
    }
}

} // namespace

/** @brief A pulse on a pin reaches the core with the width it had. */
static void test_capture(void)
{
    mock_reset();
    RcLights lights;
    CHECK(lights.begin(kPins), "begin succeeds");

    CHECK(lights.failsafe(), "no signal yet, so failsafe");

    feed(lights, 1500, 1500, 1000, 200);

    CHECK(!lights.failsafe(), "the link comes up");
    CHECK_EQ_INT(lights.pulseUs(RCL_CH_STEER), 1500, "steering pulse measured");
    CHECK_EQ_INT(lights.pulseUs(RCL_CH_THROTTLE), 1500, "throttle pulse measured");
    CHECK_EQ_INT(lights.pulseUs(RCL_CH_AUX), 1000, "switch pulse measured");
    CHECK_EQ_INT(lights.channel(RCL_CH_STEER), 0, "centre normalises to zero");

    feed(lights, 1900, 1500, 1000, 100);
    CHECK_EQ_INT(lights.pulseUs(RCL_CH_STEER), 1900, "a moved stick is measured");
    CHECK_EQ_INT(lights.channel(RCL_CH_STEER), 800, "and normalised");

    lights.end();
}

/** @brief The core's outputs land on the pins they were assigned. */
static void test_outputs(void)
{
    mock_reset();
    RcLights lights;
    CHECK(lights.begin(kPins), "begin");

    feed(lights, 1500, 1500, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 40, "front pin at park level");
    CHECK_EQ_INT(mock_analog(PIN_REAR), 30, "rear pin at park level");
    CHECK_EQ_INT(mock_analog(PIN_REVERSE), 0, "reversing pin off");
    CHECK_EQ_INT(mock_analog(PIN_AUX), 0, "aux pin off");

    feed(lights, 1500, 1800, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 255, "front pin brightens when driving");
    CHECK_EQ_INT(lights.drive(), RCL_DRIVE_FORWARD, "and the state agrees");

    feed(lights, 1500, 1200, 1000, 100);
    CHECK_EQ_INT(mock_analog(PIN_REAR), 255, "rear pin at brake level");

    /* The switch, on its middle position, drives the auxiliary output. */
    feed(lights, 1500, 1500, 1500, 100);
    CHECK_EQ_INT(mock_analog(PIN_AUX), 255, "aux pin follows the switch");

    lights.end();
}

/** @brief A pin is only written when its value has actually changed. */
static void test_writes_are_not_repeated(void)
{
    mock_reset();
    RcLights lights;
    CHECK(lights.begin(kPins), "begin");

    feed(lights, 1500, 1500, 1000, 400);
    uint32_t before = mock_writes(PIN_FRONT);
    feed(lights, 1500, 1500, 1000, 400);
    uint32_t after = mock_writes(PIN_FRONT);

    /* Hundreds of loop() calls with nothing changing must not produce hundreds
     * of analogWrite()s: on AVR each one reconfigures a timer register. */
    CHECK_EQ_INT(after, before, "a steady output is written once, not every loop");

    lights.end();
}

/** @brief Inverted outputs, for LEDs wired to the supply rail. */
static void test_inverted_outputs(void)
{
    mock_reset();
    RcLights lights;
    CHECK(lights.begin(kPins), "begin");
    feed(lights, 1500, 1500, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 40, "front pin, normal polarity");

    lights.setInvertedOutputs(1u << RCL_OUT_FRONT);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 215, "inverted at once, without waiting");
    CHECK_EQ_INT(mock_analog(PIN_REAR), 30, "and only the output that was named");

    feed(lights, 1500, 1800, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 0, "full brightness drives the pin low");

    lights.end();
}

/** @brief The three settings on the object reach the controller. */
static void test_object_settings(void)
{
    mock_reset();
    RcLights lights;

    lights.esc_mode = RCL_ESC_DIRECT_REVERSE;
    lights.aux_mode = RCL_AUX_MODE_2POS;
    CHECK(lights.begin(kPins), "begin with the fields set");

    CHECK_EQ_INT(lights.config().esc_mode, RCL_ESC_DIRECT_REVERSE, "esc_mode applied");
    CHECK_EQ_INT(lights.config().aux_mode, RCL_AUX_MODE_2POS, "aux_mode applied");

    /* And they behave: a direct-reverse ESC reverses without passing through
     * neutral, which the default mode would not do. */
    feed(lights, 1500, 1500, 1000, 300);
    feed(lights, 1500, 1800, 1000, 300);
    feed(lights, 1500, 1200, 1000, 2500);
    CHECK_EQ_INT(lights.drive(), RCL_DRIVE_REVERSE, "and the ESC mode is in force");

    lights.end();
}

/** @brief A full configuration wins, and the fields are corrected to match. */
static void test_full_config_wins(void)
{
    mock_reset();
    RcLights lights;

    rcl_config_t cfg;
    rcl_config_default(&cfg);
    cfg.esc_mode = RCL_ESC_DIRECT_REVERSE;
    cfg.aux_mode = RCL_AUX_MODE_OFF;

    /* Set the fields to the opposite of the configuration, to show which one
     * this overload listens to. */
    lights.esc_mode = RCL_ESC_BRAKE_THEN_REVERSE;
    lights.aux_mode = RCL_AUX_MODE_3POS;

    CHECK(lights.begin(kPins, cfg), "begin with a complete configuration");
    CHECK_EQ_INT(lights.config().esc_mode, RCL_ESC_DIRECT_REVERSE,
                 "the configuration is taken as given");
    CHECK_EQ_INT(lights.esc_mode, RCL_ESC_DIRECT_REVERSE,
                 "and the field is corrected to match it");
    CHECK_EQ_INT(lights.aux_mode, RCL_AUX_MODE_OFF, "both of them");

    lights.end();
}

/** @brief Any setting of the controller is a field of the object. */
static void test_settings_are_fields(void)
{
    mock_reset();
    RcLights lights;

    /* Not just the three a car normally needs: these are ordinary
     * rcl_config_t fields, reached through the inheritance rather than through
     * a list the wrapper has to keep up to date. */
    lights.level_front_park = 100;
    lights.park_lights_on = true;
    lights.fade_step = 0;
    lights.cal[RCL_CH_STEER].invert = true;

    CHECK(lights.begin(kPins), "begin");
    CHECK_EQ_INT(lights.config().level_front_park, 100, "the field reached the core");

    feed(lights, 1500, 1500, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 100, "and it is what the pin shows");

    /* The inverted channel proves it is the whole structure and not a handful
     * of copied scalars: steering right now reads as left. */
    feed(lights, 1900, 1500, 1000, 100);
    CHECK_EQ_INT(lights.channel(RCL_CH_STEER), -800, "the calibration came through too");

    lights.end();
}

/** @brief Changing a setting while running takes an apply(). */
static void test_apply_while_running(void)
{
    mock_reset();
    RcLights lights;
    CHECK(lights.begin(kPins), "begin");
    feed(lights, 1500, 1500, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 40, "the default park level");

    lights.level_front_park = 120;
    feed(lights, 1500, 1500, 1000, 100);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 40, "writing the field alone changes nothing");

    CHECK(lights.apply(), "apply is accepted");
    feed(lights, 1500, 1500, 1000, 100);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 120, "and then it takes effect");

    /* A refused apply() leaves the running settings alone. */
    lights.blink_period_ms = 0;
    CHECK(!lights.apply(), "nonsense is refused");
    CHECK_EQ_INT(lights.config().blink_period_ms, 700, "the running value survives");
    CHECK_EQ_INT(lights.config().level_front_park, 120,
                 "and so does the change that was accepted");

    lights.end();
}

/** @brief Active-low outputs are inverted before the pins are ever driven. */
static void test_output_polarity(void)
{
    mock_reset();
    RcLights lights;

    lights.outputs = RCLIGHTS_OUTPUTS_ACTIVE_LOW;
    CHECK(lights.begin(kPins), "begin");

    /* The first thing written to every pin must already be the dark level. An
     * active-low string that saw a 0 here would flash at full brightness every
     * time the car was switched on. */
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 255, "front pin dark from the first write");
    CHECK_EQ_INT(mock_analog(PIN_REAR), 255, "rear pin dark from the first write");
    CHECK_EQ_INT(mock_writes(PIN_FRONT), 1u, "and it was written exactly once");

    feed(lights, 1500, 1500, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 215, "park level, inverted");

    feed(lights, 1500, 1800, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 0, "full brightness drives the pin low");

    lights.end();
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 255, "and end() leaves it dark, not lit");
}

/** @brief Only one instance may own the interrupts. */
static void test_single_instance(void)
{
    mock_reset();
    RcLights first;
    RcLights second;

    CHECK(first.begin(kPins), "the first instance starts");
    CHECK(!second.begin(kPins), "the second is refused rather than taking over");

    first.end();
    CHECK(second.begin(kPins), "and can start once the first has let go");
    second.end();
}

/** @brief A configured channel 3 with nowhere to read it from is refused. */
static void test_aux_without_pin(void)
{
    mock_reset();
    RcLights lights;

    RcLightsPins pins = kPins;
    pins.ch3 = RCLIGHTS_PIN_NONE;

    rcl_config_t cfg;
    rcl_config_default(&cfg); /* aux_mode is 3POS by default */
    CHECK(!lights.begin(pins, cfg), "a switch with no pin is a mistake, not a mode");

    cfg.aux_mode = RCL_AUX_MODE_OFF;
    CHECK(lights.begin(pins, cfg), "with the switch turned off it starts");

    feed(lights, 1500, 1500, 1000, 300);
    CHECK(!lights.failsafe(), "and a two-channel receiver is enough");
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 40, "park lights on");

    lights.end();
}

/** @brief Unconnected outputs are simply not driven. */
static void test_partial_wiring(void)
{
    mock_reset();
    RcLights lights;

    RcLightsPins pins = kPins;
    pins.reverse = RCLIGHTS_PIN_NONE;
    pins.aux = RCLIGHTS_PIN_NONE;
    CHECK(lights.begin(pins), "begin with two outputs unwired");

    feed(lights, 1500, 1500, 1500, 300);
    CHECK_EQ_INT(mock_writes(PIN_REVERSE), 0, "an unwired pin is never touched");
    CHECK_EQ_INT(mock_writes(PIN_AUX), 0, "nor is the other one");
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 40, "the wired ones still work");

    lights.end();
}

/** @brief Losing the receiver, seen from the pins. */
static void test_failsafe_on_pins(void)
{
    mock_reset();
    RcLights lights;
    CHECK(lights.begin(kPins), "begin");
    feed(lights, 1500, 1800, 1000, 400);

    quiet(lights, 1000);
    CHECK(lights.failsafe(), "silence trips the failsafe");
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 0, "front pin dark");
    CHECK_EQ_INT(mock_analog(PIN_REAR), 0, "rear pin dark");
    CHECK_EQ_INT(mock_analog(PIN_LEFT), mock_analog(PIN_RIGHT),
                 "the hazards blink together");

    feed(lights, 1500, 1500, 1000, 200);
    CHECK(!lights.failsafe(), "and it recovers by itself");

    lights.end();
}

/** @brief Taking the current stick positions as the centre. */
static void test_capture_center(void)
{
    mock_reset();
    RcLights lights;

    /* Automatic centring off: this case is about the manual call, and with the
     * automatic one running there would be nothing left for it to correct. */
    rcl_config_t cfg;
    rcl_config_default(&cfg);
    cfg.auto_center_ms = 0;
    CHECK(lights.begin(kPins, cfg), "begin");

    /* A transmitter whose steering trim sits 60 µs off. */
    feed(lights, 1560, 1500, 1000, 300);
    CHECK_EQ_INT(lights.channel(RCL_CH_STEER), 120, "off-centre before calibration");

    CHECK(lights.captureCenter(), "the centre is taken");
    feed(lights, 1560, 1500, 1000, 100);
    CHECK_EQ_INT(lights.channel(RCL_CH_STEER), 0, "and now reads centred");
    CHECK_EQ_INT(lights.config().cal[RCL_CH_STEER].center_us, 1560,
                 "the configuration holds the measured centre");

    lights.end();
}

/** @brief end() switches everything off and stops touching the pins. */
static void test_shutdown(void)
{
    mock_reset();
    RcLights lights;
    CHECK(lights.begin(kPins), "begin");
    feed(lights, 1500, 1800, 1000, 300);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 255, "lit");

    lights.end();
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 0, "end() switches the outputs off");

    uint32_t writes = mock_writes(PIN_FRONT);
    feed(lights, 1500, 1800, 1000, 300);
    CHECK_EQ_INT(mock_writes(PIN_FRONT), writes, "and loop() then does nothing");
}

/** @brief Rejected settings leave the working ones in place. */
static void test_apply_config(void)
{
    mock_reset();
    RcLights lights;
    CHECK(lights.begin(kPins), "begin");
    feed(lights, 1500, 1500, 1000, 300);

    rcl_config_t cfg = lights.config();
    cfg.level_front_park = 100;
    CHECK(lights.applyConfig(cfg), "a valid change is taken");
    feed(lights, 1500, 1500, 1000, 200);
    CHECK_EQ_INT(mock_analog(PIN_FRONT), 100, "and takes effect");

    cfg.blink_period_ms = 0;
    CHECK(!lights.applyConfig(cfg), "an invalid one is refused");
    CHECK_EQ_INT(lights.config().blink_period_ms, 700, "the old value survives");
    CHECK_EQ_INT(lights.config().level_front_park, 100,
                 "and so does the change that was accepted");

    lights.end();
}

/**
 * @brief Run every case.
 * @return 0 when they all passed.
 */
int main(void)
{
    test_begin("arduino");

    test_capture();
    test_outputs();
    test_writes_are_not_repeated();
    test_inverted_outputs();
    test_object_settings();
    test_full_config_wins();
    test_settings_are_fields();
    test_apply_while_running();
    test_output_polarity();
    test_single_instance();
    test_aux_without_pin();
    test_partial_wiring();
    test_failsafe_on_pins();
    test_capture_center();
    test_shutdown();
    test_apply_config();

    return test_end();
}
