/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file RcLightsCalibrate.ino
 * @brief Measure your receiver, then watch what the controller makes of it.
 *
 * Two things at once, because in practice they are done together:
 *
 *   - **Measure.** It records the smallest and largest pulse it has seen on
 *     each channel, so moving every stick to both stops gives you the endpoint
 *     numbers to paste into your own sketch.
 *   - **Watch.** It prints what the controller currently thinks — the drive
 *     state, the turn signals, every output — so you can see *why* a light is
 *     doing what it is doing, rather than guessing from the LED.
 *
 * Send `c` over the serial port with the sticks at rest to take the centres,
 * and `r` to start the measurement again.
 *
 * The numbers are not saved anywhere. Copy them into the `#define`s at the top
 * of your RcLightsCar sketch; writing them to EEPROM or to NVS is a per-board
 * matter this library stays out of.
 *
 * The pins are this board's defaults, which are the same ones RcLightsCar
 * starts from — so a car wired for that sketch can run this one without
 * rewiring anything.
 */

#include <RcLights.h>

RcLights lights;

/** @brief Smallest pulse seen per channel since the last reset. */
uint16_t seenMin[RCL_CH_COUNT];
/** @brief Largest pulse seen per channel since the last reset. */
uint16_t seenMax[RCL_CH_COUNT];

/** @brief Start the endpoint measurement over. */
void resetSpan()
{
    for (int i = 0; i < RCL_CH_COUNT; i++) {
        seenMin[i] = 0xFFFF;
        seenMax[i] = 0;
    }
    Serial.println(F("--- measuring again; move every stick to both stops ---"));
}

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        /* Wait for a native-USB port to come up, but not forever: a car on the
         * bench has no serial monitor attached and must still light up. */
    }

    if (!lights.begin()) {
        Serial.println(F("RcLights: begin() failed"));
        while (true) {
        }
    }

    resetSpan();
    Serial.println(F("c = take centres   r = measure again"));
}

/** @brief Print one line per channel and one for the outputs. */
void report()
{
    Serial.println();
    for (int i = 0; i < RCL_CH_COUNT; i++) {
        rcl_channel_t ch = (rcl_channel_t)i;
        Serial.print(F("ch"));
        Serial.print(i + 1);
        Serial.print(lights.channelValid(ch) ? F("  live ") : F("  DEAD "));
        Serial.print(F(" now="));
        Serial.print(lights.pulseUs(ch));
        Serial.print(F("us  min="));
        Serial.print(seenMin[i] == 0xFFFF ? 0 : seenMin[i]);
        Serial.print(F("  max="));
        Serial.print(seenMax[i]);
        Serial.print(F("  centre="));
        Serial.print(lights.config().cal[i].center_us);
        Serial.print(F("  -> "));
        Serial.println(lights.channel(ch));
    }

    if (rcl_centering(&lights.state()))
        Serial.println(F("measuring the stick centres; leave them alone"));

    Serial.print(F("drive="));
    Serial.print(rcl_drive_name(lights.drive()));
    if (lights.failsafe())
        Serial.print(F("  FAILSAFE"));
    Serial.print(F("   front="));
    Serial.print(lights.output(RCL_OUT_FRONT));
    Serial.print(F(" rear="));
    Serial.print(lights.output(RCL_OUT_REAR));
    Serial.print(F(" rev="));
    Serial.print(lights.output(RCL_OUT_REVERSE));
    Serial.print(F(" L="));
    Serial.print(lights.output(RCL_OUT_TURN_LEFT));
    Serial.print(F(" R="));
    Serial.print(lights.output(RCL_OUT_TURN_RIGHT));
    Serial.print(F(" aux="));
    Serial.println(lights.output(RCL_OUT_AUX));
}

void loop()
{
    lights.loop();

    for (int i = 0; i < RCL_CH_COUNT; i++) {
        rcl_channel_t ch = (rcl_channel_t)i;
        if (!lights.channelValid(ch))
            continue;
        uint16_t us = lights.pulseUs(ch);
        if (us < seenMin[i])
            seenMin[i] = us;
        if (us > seenMax[i])
            seenMax[i] = us;
    }

    if (Serial.available()) {
        int c = Serial.read();
        if (c == 'c') {
            Serial.println(lights.captureCenter() ? F("centres taken")
                                                  : F("centres refused: is the link up, "
                                                      "and are the sticks at rest?"));
        } else if (c == 'r') {
            resetSpan();
        }
    }

    static uint32_t last;
    if (millis() - last >= 500) {
        last = millis();
        report();
    }
}
