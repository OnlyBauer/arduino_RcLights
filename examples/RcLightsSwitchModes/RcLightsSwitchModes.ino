/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLightsSwitchModes.ino
 * @brief Channel 3: on/off or three-way, hazards or auxiliary, your choice.
 *
 * You say how many positions the switch has and what each one does, so the same
 * sketch fits a toggle on one transmitter and a three-way on the next. Pick one
 * by changing #SETUP.
 *
 * The actions are ACTION_NOTHING, ACTION_HAZARDS (both signals blink),
 * ACTION_AUX (auxiliary output on) and ACTION_LIGHTS_OFF (park and driving
 * light off; brake, reverse and signals keep working).
 *
 * SWITCH_NONE means no channel 3: the pin is not read and its absence does not
 * trip the failsafe. Pins are this board's defaults, as in RcLightsCar.
 */

#include <RcLights.h>

/** @brief Which of the setups below to compile: 1, 2, 3 or 4. */
#define SETUP 1

RcLights lights;

void setup()
{
    Serial.begin(115200);

#if SETUP == 1
    /* The defaults: down nothing, middle light bar, up hazards. */
    lights.aux_mode = SWITCH_3POS;
    lights.aux_action[0] = ACTION_NOTHING;
    lights.aux_action[1] = ACTION_AUX;
    lights.aux_action[2] = ACTION_HAZARDS;

#elif SETUP == 2
    /* A plain toggle for the hazards. The middle entry is ignored, and the gap
     * between aux_low and aux_high becomes hysteresis. */
    lights.aux_mode = SWITCH_2POS;
    lights.aux_action[0] = ACTION_NOTHING;
    lights.aux_action[2] = ACTION_HAZARDS;

#elif SETUP == 3
    /* A light switch as on a real car: off, lights, light bar. Brake, reverse
     * and indicators keep working in the off position, as they do on a car. */
    lights.aux_mode = SWITCH_3POS;
    lights.aux_action[0] = ACTION_LIGHTS_OFF;
    lights.aux_action[1] = ACTION_NOTHING;
    lights.aux_action[2] = ACTION_AUX;

#else
    /* A two-channel receiver: nothing is wired to the third input. */
    lights.aux_mode = SWITCH_NONE;
#endif

    /* Where the positions sit, either side of a nominal 1500 µs middle. */
    lights.aux_low = -300;
    lights.aux_high = 300;

    RcLightsPins pins = RCLIGHTS_PINS_DEFAULT;
#if SETUP == 4
    pins.ch3 = RCLIGHTS_PIN_NONE;
#endif

    if (!lights.begin(pins)) {
        Serial.println(F("RcLights: begin() refused the settings"));
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
