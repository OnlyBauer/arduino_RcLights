/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLightsSwitchModes.ino
 * @brief Channel 3: on/off or three-way, hazards or auxiliary, your choice.
 *
 * What the switch on channel 3 does is not fixed by the library. You say how
 * many positions it has and what each one is for, and the same sketch then fits
 * a two-position toggle on one transmitter and a three-position switch on the
 * next.
 *
 * Pick a setup below by changing #SETUP.
 *
 * | Position | ::rcl_aux_action_t | Effect |
 * | --- | --- | --- |
 * | | ::RCL_AUX_ACTION_NONE | nothing |
 * | | ::RCL_AUX_ACTION_HAZARD | both turn signals blink together |
 * | | ::RCL_AUX_ACTION_AUX | the auxiliary output switches on |
 * | | ::RCL_AUX_ACTION_LIGHTS_OFF | park and driving light off; brake, reverse and turn signals keep working |
 *
 * A car with no channel 3 at all sets ::RCL_AUX_MODE_OFF, and then the third
 * input pin is not read and its absence does not trip the failsafe.
 *
 * The pins here are this board's defaults, which are the same ones the
 * RcLightsCar example uses. That sketch is the one to start from for a real
 * car; this one exists to vary a single setting.
 */

#include <RcLights.h>

/** @brief Which of the setups below to compile: 1, 2, 3 or 4. */
#define SETUP 1

RcLights lights;

void setup()
{
    Serial.begin(115200);

    rcl_config_t cfg;
    rcl_config_default(&cfg);

#if SETUP == 1
    /* A three-position switch, which is what the defaults assume:
     * down — nothing, middle — light bar, up — hazards. */
    cfg.aux_mode = RCL_AUX_MODE_3POS;
    cfg.aux_action[0] = RCL_AUX_ACTION_NONE;
    cfg.aux_action[1] = RCL_AUX_ACTION_AUX;
    cfg.aux_action[2] = RCL_AUX_ACTION_HAZARD;

#elif SETUP == 2
    /* A plain on/off toggle for the hazards. The middle entry is ignored in
     * two-position mode; the gap between aux_low and aux_high becomes
     * hysteresis, so a switch that reads slightly differently each time does
     * not chatter. */
    cfg.aux_mode = RCL_AUX_MODE_2POS;
    cfg.aux_action[0] = RCL_AUX_ACTION_NONE;
    cfg.aux_action[2] = RCL_AUX_ACTION_HAZARD;

#elif SETUP == 3
    /* A light switch, as on a real car: off, park and driving lights, plus a
     * light bar at the top. Note that the brake light, the reversing light and
     * the indicators keep working in the "off" position — they do on a real car
     * too, and for the same reason. */
    cfg.aux_mode = RCL_AUX_MODE_3POS;
    cfg.aux_action[0] = RCL_AUX_ACTION_LIGHTS_OFF;
    cfg.aux_action[1] = RCL_AUX_ACTION_NONE;
    cfg.aux_action[2] = RCL_AUX_ACTION_AUX;

#else
    /* A two-channel receiver, or a channel 3 you would rather use for
     * something else. Nothing is wired to the third input and nothing waits
     * for it. */
    cfg.aux_mode = RCL_AUX_MODE_OFF;
#endif

    /* Where the positions sit. A three-position switch usually gives about
     * 1000, 1500 and 2000 µs; these thresholds land either side of the middle
     * one with room to spare. */
    cfg.aux_low = -300;
    cfg.aux_high = 300;

    RcLightsPins pins = RCLIGHTS_PINS_DEFAULT;
#if SETUP == 4
    pins.ch3 = RCLIGHTS_PIN_NONE;
#endif

    if (!lights.begin(pins, cfg)) {
        Serial.println(F("RcLights: begin() refused the configuration"));
        while (true) {
        }
    }
}

void loop()
{
    lights.loop();

    /* Show what the switch is doing, once a second. */
    static uint32_t last;
    if (millis() - last >= 1000) {
        last = millis();
        Serial.print(F("ch3 = "));
        Serial.print(lights.pulseUs(RCL_CH_AUX));
        Serial.print(F(" us   hazard="));
        Serial.print(lights.state().hazard ? F("on") : F("off"));
        Serial.print(F("   aux="));
        Serial.println(lights.state().aux_on ? F("on") : F("off"));
    }
}
