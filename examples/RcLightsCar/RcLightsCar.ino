/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLightsCar.ino
 * @brief A complete light controller for an RC car. Wire it up, set the three
 *        options below, drive.
 */

#include <RcLights.h>

/*
 * WIRING
 *
 * Connect the receiver's ground to the board's ground. Each LED goes to its pin
 * through a resistor; anything brighter than one LED needs a transistor.
 *
 *                      Nano    Nucleo   ESP32
 *   steering  (ch1)     D4       D2      GPIO 34
 *   throttle  (ch2)     D7       D4      GPIO 35
 *   switch    (ch3)     D8       D7      GPIO 32
 *   front light         D3       D3      GPIO 13
 *   rear light          D5       D5      GPIO 14
 *   reverse light       D6       D6      GPIO 27
 *   signal left         D9       D9      GPIO 26
 *   signal right       D10      D10      GPIO 25
 *   aux                D11      D11      GPIO 33
 *
 * To move one, #define its pin above the include (or pass
 * -DRCLIGHTS_PIN_FRONT=6 from the build). RCLIGHTS_PIN_NONE for anything you
 * have not wired.
 *
 * The stick centres are measured in the first fifth of a second after the
 * receiver comes up, so leave the sticks alone when you switch on.
 */

/**
  * @brief LEDS_ACTIVE_HIGH if your LEDs go from the pin to ground,
  *        LEDS_ACTIVE_LOW if they go from the supply rail to the pin.
  */
#define OUTPUTS LEDS_ACTIVE_HIGH

/**
  * @brief ESC_BRAKE_THEN_REVERSE if a backwards stick brakes and reverse needs
  *        neutral first, ESC_DIRECT_REVERSE if it reverses straight out of the
  *        brake.
  */
#define ESC_MODE ESC_BRAKE_THEN_REVERSE

/**
  * @brief SWITCH_3POS for a three-position switch (middle = aux, up = hazards),
  *        SWITCH_2POS for a toggle (up = hazards), SWITCH_NONE if you have no
  *        third channel.
  */
#define CH3_MODE SWITCH_3POS

/** @brief The controller. */
RcLights lights;

void setup()
{
    lights.esc_mode = ESC_MODE;
    lights.aux_mode = CH3_MODE;
    lights.outputs = OUTPUTS;

    /* ================= OPTIONAL =================
     *
     * Every setting, with the value it already uses. Delete the block and the
     * car drives the same; keep the lines you want to change.
     */

    /* Brightness, 0 to 255. What matters is the step from park to brake. */
    lights.level_front_park = 40;   /* front LED standing still */
    lights.level_front_drive = 255; /* front LED while driving */
    lights.level_rear_park = 30;    /* rear LED not braking */
    lights.level_rear_brake = 255;  /* rear LED while braking */
    lights.level_reverse = 255;     /* reversing light */
    lights.level_turn = 255;        /* turn signals */
    lights.level_aux = 255;         /* auxiliary output */
    lights.park_lights_on = true;   /* park and tail light on at all */
    lights.fade_step = 40;          /* fade speed; 0 switches instantly */

    /* Turn signals. They only start once the steering has been still for
     * center_hold_ms, which tells a corner from a correction. */
    lights.steer_trigger = 350;     /* how far to steer before it indicates */
    lights.steer_release = 200;     /* how far back before it stops */
    lights.steer_center_band = 120; /* steering this small counts as straight */
    lights.center_hold_ms = 400;    /* how long straight before it arms */
    lights.turn_min_ms = 900;       /* shortest a signal ever runs */
    lights.blink_period_ms = 700;   /* one blink, on plus off */
    lights.blink_duty = 50;         /* percent of that spent lit */

    /* Throttle. coast_ms stands in for the speed sensor there is not: while it
     * runs a backwards stick brakes, after it the same stick reverses. */
    lights.coast_ms = 1500;            /* how long the car keeps rolling */
    lights.brake_threshold = 200;      /* backwards stick this big is braking */
    lights.throttle_center_band = 100; /* throttle this small counts as neutral */
    lights.reverse_arm_ms = 250;       /* hold back this long to select reverse */
    lights.brake_extend_ms = 400;      /* brake light holds on after release */

    /* Channel 3: low, middle and high position. The middle one is unused on a
     * two-position switch. */
    lights.aux_action[0] = ACTION_NOTHING;
    lights.aux_action[1] = ACTION_AUX;
    lights.aux_action[2] = ACTION_HAZARDS;
    lights.aux_low = -300; /* below this is the low position */
    lights.aux_high = 300; /* above this is the high position */

    /* The receiver. The centres look after themselves; run RcLightsCalibrate
     * to measure the endpoints. invert if a channel works the wrong way. */
    lights.cal[RCL_CH_STEER].min_us = 1000;
    lights.cal[RCL_CH_STEER].max_us = 2000;
    lights.cal[RCL_CH_STEER].invert = false;
    lights.cal[RCL_CH_THROTTLE].min_us = 1000;
    lights.cal[RCL_CH_THROTTLE].max_us = 2000;
    lights.cal[RCL_CH_THROTTLE].invert = false;
    lights.cal[RCL_CH_AUX].min_us = 1000;
    lights.cal[RCL_CH_AUX].max_us = 2000;
    lights.cal[RCL_CH_AUX].invert = false;

    /* Signal handling. */
    lights.auto_center_ms = 200;    /* time spent measuring the stick centres */
    lights.signal_timeout_ms = 500; /* silence this long means the link is gone */
    lights.failsafe_hazard = true;  /* blink the hazards when it is */
    lights.pulse_min_us = 700;      /* pulses outside this range are ignored */
    lights.pulse_max_us = 2300;

    /* =============== END OPTIONAL =============== */

    lights.begin();
}

void loop()
{
    lights.loop();
}

/*
 * IF SOMETHING IS WRONG
 *
 * No lights at all:                    begin() refused the settings. Check
 *                                      CH3_MODE against RCLIGHTS_PIN_CH3.
 * Indicators on the wrong side:        lights.cal[RCL_CH_STEER].invert = true
 * Brake and forward swapped:           lights.cal[RCL_CH_THROTTLE].invert = true
 * Reverse light on when braking:       try the other ESC_MODE, then raise
 *                                      lights.coast_ms
 * Indicators come on when correcting:  raise lights.steer_trigger or
 *                                      lights.center_hold_ms
 * Lights too bright for a model:       lights.level_front_park and friends
 * Some LEDs one way round and some
 * the other:                           lights.setInvertedOutputs(
 *                                          1u << RCL_OUT_FRONT);  // after begin()
 *
 * To change a setting while the car is running, write the field and call
 * lights.apply().
 *
 * The RcLightsCalibrate example prints what the controller is measuring and
 * deciding, which is the quickest way to find out why a light is doing what it
 * is doing.
 */
