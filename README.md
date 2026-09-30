# RcLights

A scale light controller for an RC car. Three receiver channels in, six PWM
light outputs out.

```text
        ch1 steering  ──┐                    ┌──  front   park / driving light
        ch2 throttle  ──┼──►  RcLights  ─────┼──  rear    tail / brake light
        ch3 switch    ──┘                    ├──  reverse
                                             ├──  turn left
                                             ├──  turn right
                                             └──  aux     light bar, beacon, ...
```

What it does, out of the box:

- **Tail and brake light on one LED.** Dim while the car is standing or
  driving, full brightness while braking, held for a moment after the brake is
  released so a stab at the brakes is still visible.
- **Park and driving light on one LED.** Dim at rest, brighter while moving.
  Both LEDs fade between their two levels rather than stepping, which reads as
  a lamp rather than as a delay.
- **A reversing light** that is on while the car is actually reversing — and
  not while it is braking, which is the part that takes some care.
- **Turn signals that indicate rather than twitch.** They only start once the
  steering has been centred for a moment, so taking a corner sets them flashing
  and correcting the line mid-corner does not.
- **Hazards, or an auxiliary output, or a light switch** on channel 3, as a
  two- or three-position switch, with each position assigned to whatever you
  want it to do.
- **Hazards on signal loss**, recovering by itself when the transmitter comes
  back.

Runs on an Arduino Nano, an ST Nucleo and an ESP32 DevKit from the same source.

> **Status: 0.1.0, not verified on a car.** It compiles for all three targets
> and passes 289 host checks. Nobody has driven it yet. See
> [CHANGELOG.md](CHANGELOG.md) for what that specifically means.

## Installing

Not in the Arduino Library Manager yet — see
[CHANGELOG.md](CHANGELOG.md#publishing-to-the-arduino-library-manager) for what
that takes. Until then:

- **Arduino IDE**: download the repository as a ZIP and use *Sketch → Include
  Library → Add .ZIP Library*.
- **Anywhere else**: clone it into your sketchbook's `libraries/` folder.
- **PlatformIO**: point `lib_deps` at the repository URL; `library.json` is in
  the root.

## Getting started

```cpp
#include <RcLights.h>

RcLights lights;

void setup()
{
    lights.begin();     // this board's default pins
}

void loop()
{
    lights.loop();      // call as often as you like
}
```

That is the complete `RcLightsBasic` example. Upload it, open the serial
monitor at 115200 baud, and it prints which pins it is using.

For a real car, start from **`RcLightsCar`**. It is a complete implementation
and it asks you for three things:

```cpp
void setup()
{
    lights.esc_mode = RCL_ESC_BRAKE_THEN_REVERSE;   // or RCL_ESC_DIRECT_REVERSE
    lights.aux_mode = RCL_AUX_MODE_3POS;            // or 2POS, or OFF
    lights.outputs  = RCLIGHTS_OUTPUTS_ACTIVE_HIGH; // LEDs switched to ground

    lights.begin();
}
```

Everything else has a working default, including the pins, which the library
chooses from the board you are compiling for, and the stick centres, which it
measures at startup. Below those three lines the sketch has an **optional**
block listing every other setting there is, each one already set to the value
it has — delete the block and nothing changes, keep a line and change it and
only that changes. A test compiles that sketch and fails if a value in the
block ever stops matching the library's own default.

The four examples:

| Example | What it shows |
| --- | --- |
| `RcLightsCar` | **a complete car: set the pins, upload, drive** |
| `RcLightsBasic` | the whole library in fifteen lines |
| `RcLightsCalibrate` | measure your receiver, and watch what the controller makes of it |
| `RcLightsSwitchModes` | the four things channel 3 can be |

## Wiring

The library picks the pins from the board you compile for. You do not have to
set any of them.

| | Nano / Uno | Nucleo-64 | ESP32 DevKit |
| --- | --- | --- | --- |
| ch1 steering | D4 | D2 | GPIO 34 |
| ch2 throttle | D7 | D4 | GPIO 35 |
| ch3 switch | D8 | D7 | GPIO 32 |
| front | D3 | D3 | GPIO 13 |
| rear | D5 | D5 | GPIO 14 |
| reverse | D6 | D6 | GPIO 27 |
| turn left | D9 | D9 | GPIO 26 |
| turn right | D10 | D10 | GPIO 25 |
| aux | D11 | D11 | GPIO 33 |

Two rules picked every one of those: an input has to be able to raise an
interrupt, and an output has to have a timer channel behind it. On the Nano the
six hardware PWM pins are exactly the six outputs, which is why the inputs are
on pins that have none.

To move one, set its macro from the build — every pin is guarded, so whatever
you define wins:

```ini
; platformio.ini
build_flags = -DRCLIGHTS_PIN_FRONT=6 -DRCLIGHTS_PIN_AUX=RCLIGHTS_PIN_NONE
```

```sh
arduino-cli compile --build-property "compiler.cpp.extra_flags=-DRCLIGHTS_PIN_FRONT=6" ...
```

The Arduino IDE has no field for build flags; there, define the macro in the
sketch above `#include <RcLights.h>`. The names are `RCLIGHTS_PIN_STEERING`,
`_THROTTLE`, `_CH3`, `_FRONT`, `_REAR`, `_REVERSE`, `_SIGNAL_LEFT`,
`_SIGNAL_RIGHT` and `_AUX`. Anything you have not wired gets
`RCLIGHTS_PIN_NONE` and is never driven, so a car with no reversing light needs
no other change.

The outputs are logic-level PWM, not LED drivers. A single indicator LED runs
straight off a pin through a resistor; a light bar, a string, or anything above
20 mA wants a transistor or a small MOSFET. If your LEDs are wired to the
positive rail instead of to ground, say so rather than rewiring:

```cpp
lights.outputs = RCLIGHTS_OUTPUTS_ACTIVE_LOW;   // before begin()
```

`begin()` applies that before it first drives the pins, so an active-low string
does not flash at full brightness on the way up. For a car wired both ways
round, leave `outputs` alone and call `setInvertedOutputs(mask)` after `begin()`
with one bit per output.

### Receiver

The three channels are ordinary servo outputs; nothing about the library is
specific to a protocol. Steering on channel 1, throttle on channel 2, a switch
on channel 3 — or in whatever order your receiver labels them, since the pin map
is yours to write. Receiver ground and the board's ground have to be connected.

## Setting it up for your car

**The stick centres look after themselves.** For the first 200 ms after the
receiver comes up, the library averages what the steering and throttle are
sending and takes that as their rest position, so transmitter trim needs no
setting anywhere. Leave the sticks alone when you switch on. If they move while
it is measuring, or if what it measures is more than 200 µs from nominal — a
stick being held rather than a trim offset — it keeps the configured centre
instead, so holding full throttle at power-up cannot calibrate full throttle as
neutral.

The endpoints are still nominal 1000/2000 µs. If the indicators trigger at the
wrong point, run `RcLightsCalibrate`, move every stick to both stops, and set
what it prints:

```cpp
cfg.cal[RCL_CH_STEER].min_us = 1004;
cfg.cal[RCL_CH_STEER].max_us = 1996;
```

**Your ESC** is the one thing that has to be told, and the one that cannot be
guessed:

```cpp
lights.esc_mode = RCL_ESC_BRAKE_THEN_REVERSE;  // back stick brakes; reverse needs neutral
lights.esc_mode = RCL_ESC_DIRECT_REVERSE;      // back stick reverses out of the brake
```

There is no speed sensor, so "is the car still moving?" is inferred from the
throttle history over `coast_ms`. Set it to roughly how long your car takes to
coast to a stop from half throttle. Too long and the reversing light is slow to
appear; too short and braking from speed turns into reverse.

Everything else has a default that works, and every setting is a field of the
object — `RcLights` derives from
[`rcl_config_t`](src/rclights_core.h), so there is no second list to fall out of
step with the first:

```cpp
lights.level_front_park = 20;           // dimmer park light
lights.brake_extend_ms = 600;           // hold the brake light a little longer
lights.cal[RCL_CH_STEER].invert = true; // indicators on the wrong side
lights.begin();
```

Write the fields, call `begin()`. To change one while running, write it and
call `apply()`. A sketch that keeps its settings elsewhere — read back from
EEPROM, say — passes them as an `rcl_config_t` to `begin(pins, cfg)` or
`applyConfig(cfg)` instead, and the object's fields are then updated to match
what was applied. Every route validates first, and settings that cannot be
acted on are refused rather than acted on badly.

## How it is put together

```text
src/rclights_core.h/.c     the controller: plain C11, no Arduino, no I/O
src/RcLights.h/.cpp        Arduino: capture pulses, write PWM, nothing else
src/rclights_board.h       pin maps and the three facts that differ per board
```

The split is not decoration. Everything the lights do — the brake and reverse
state machine, the turn signal arming rule, the blink phase, the failsafe — is
in the core, and the core has no dependency beyond `<stdint.h>`. That is what
lets `tests/test_core.c` fly the whole thing through stick sequences on a host,
in a second, with no board involved, and it is why the same logic would drop
into an ESP-IDF or STM32 HAL project unchanged.

The Arduino layer is deliberately dull: it timestamps edges in an interrupt,
hands the widths to the core, and writes what comes back.

### The AVR problem, and what is done about it

Three receiver channels need three edge-triggered inputs. An ATmega328P has two
external interrupt pins. RcLights therefore installs its own pin-change
interrupt handlers on AVR and **defines `PCINT0_vect`, `PCINT1_vect` and
`PCINT2_vect`** — so it will not link alongside another library that does the
same, which several softserial and RC receiver libraries do. If you hit that:

```cpp
#define RCLIGHTS_NO_AVR_PCINT   // before #include <RcLights.h>
```

and wire the channels you care about to D2 and D3. Nothing else on any other
board is affected; Nucleo and ESP32 can attach an interrupt to any pin.

## Building and testing

The library needs nothing but a board core. To work on it:

```sh
tools/ci.sh            # everything this machine can run
tools/ci.sh lint       # formatting, clang-tidy, versions, example layout
make -C tests          # the two host suites
make -C tests sanitize # the same under ASan and UBSan
doxygen Doxyfile       # API docs into docs/html; warnings are errors
```

`tools/ci.sh` is what `.gitlab-ci.yml` calls, so a green run here is a green
pipeline — with the exception of the `build-examples` job, which compiles the
examples for Nano, ESP32 DevKit and Nucleo-64 and needs the toolchains.

Three suites, and the split between the first two mirrors the source:

| Suite | What it tests |
| --- | --- |
| `test_core` | the controller, through stick sequences, with no board |
| `test_arduino_port` | the real wrapper against a mock Arduino runtime that delivers pulses as edges on pins |
| `test_example_defaults` | the shipping `RcLightsCar.ino`, compiled as it ships, checking its optional block still lists the library's defaults |

The AVR pin-change capture is not covered by any of them — it is compiled out
on a host, and only a Nano can show whether it works.

## Licence

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
