/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLightsBasic.ino
 * @brief The whole library in fifteen lines: default pins, default behaviour.
 *
 * Wire the receiver to the three input pins and the LEDs to the six output
 * pins your board's default map names — `RcLights.h` prints them over the
 * serial port at startup, so there is no need to look them up.
 *
 * What you get:
 *
 *   - park light and tail light on as soon as the receiver is live,
 *   - front light brighter while driving, tail light full while braking,
 *   - reversing light while reversing,
 *   - turn signals when you steer after running straight,
 *   - hazards from the top position of the channel 3 switch, auxiliary output
 *     from the middle one,
 *   - hazards flashing on their own if the transmitter is switched off.
 *
 * Every one of those is adjustable, and for a real car you want the
 * RcLightsCar example rather than this one: it is the same thing with every
 * pin, brightness and behaviour as a `#define` at the top of the file. This
 * sketch is here to show how little the library needs to be told.
 */

#include <RcLights.h>

RcLights lights;

void setup()
{
    Serial.begin(115200);

    if (!lights.begin()) {
        /* The only ways this fails with the default pins are a second RcLights
         * object or a board whose default map names a pin that cannot raise an
         * interrupt. Both are wiring or sketch mistakes, not runtime faults, so
         * say so and stop rather than running with the lights dead. */
        Serial.println(F("RcLights: begin() failed"));
        while (true) {
        }
    }

    const RcLightsPins &p = lights.pins();
    Serial.print(F("RcLights on "));
    Serial.println(F(RCLIGHTS_BOARD_NAME));
    Serial.print(F("  inputs  ch1="));
    Serial.print(p.ch1);
    Serial.print(F(" ch2="));
    Serial.print(p.ch2);
    Serial.print(F(" ch3="));
    Serial.println(p.ch3);
    Serial.print(F("  outputs front="));
    Serial.print(p.front);
    Serial.print(F(" rear="));
    Serial.print(p.rear);
    Serial.print(F(" reverse="));
    Serial.print(p.reverse);
    Serial.print(F(" left="));
    Serial.print(p.turnLeft);
    Serial.print(F(" right="));
    Serial.print(p.turnRight);
    Serial.print(F(" aux="));
    Serial.println(p.aux);
}

void loop()
{
    /* Nothing blocks and nothing is timed from here: the controller works from
     * millis(), so calling this more or less often only changes how smooth the
     * fade looks. */
    lights.loop();
}
