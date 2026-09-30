/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLightsBasic.ino
 * @brief The whole library with default pins and default behaviour. It prints
 *        the pins it is using over the serial port at startup.
 *
 * For a real car, start from RcLightsCar instead; this sketch is here to show
 * how little the library needs to be told.
 */

#include <RcLights.h>

RcLights lights;

void setup()
{
    Serial.begin(115200);

    if (!lights.begin()) {
        /* A sketch mistake, not a runtime fault: say so and stop. */
        Serial.println(F("RcLights: begin() failed"));
        while (true) {
        }
    }

    RcLightsPins p = lights.pins();
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
    /* Nothing blocks here; the controller works from millis(). */
    lights.loop();
}
