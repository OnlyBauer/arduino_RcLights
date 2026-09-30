# Changelog

All notable changes to this library, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Tagging

| Tag | `library.properties` and `library.json` |
| --- | --- |
| `release_X.Y.Z` | `X.Y.Z` |
| `prerelease_X.Y.Z_dev` | `X.Y.Z-dev` |

`library.properties` is the authority, because the Arduino Library Manager reads
that and nothing else. The `version-matches-tag` CI job checks it against the tag
and against `library.json` and the `Doxyfile`, which must agree.

## [Unreleased]

Nothing yet.

## [0.1.0]

First version. Everything below is new.

### Added

- **The controller** (`src/rclights_core.c`, `src/rclights_core.h`). Plain C11,
  no Arduino, no I/O: three normalised stick positions and a millisecond clock
  go in, six brightness values come out. Every decision the lights make happens
  here, which is what makes them testable without a car.
  - Park and driving light on one front LED, tail and brake light on one rear
    LED, both fading between their two levels.
  - A drive state machine over the throttle with two ESC behaviours:
    `RCL_ESC_BRAKE_THEN_REVERSE`, where a backwards stick brakes and reverse
    needs a pass through neutral, and `RCL_ESC_DIRECT_REVERSE`, where it does
    not. Rolling is inferred from the throttle history, because a light
    controller has no speed sensor.
  - Turn signals that only trigger once the steering has been centred for
    `center_hold_ms`, so cornering indicates and mid-corner line corrections do
    not. Hysteresis between the trigger and release thresholds, a minimum
    running time, and an end that always falls on a completed blink cycle.
  - Channel 3 as a two- or three-position switch, with each position assigned
    to one of: nothing, hazards, an auxiliary output, or a light switch that
    leaves the brake, reverse and turn signals working.
  - Automatic centring: the rest position of the steering and throttle is
    measured over the first `auto_center_ms` of signal — timed from the first
    pulse, not from power-up, so it works when the car is switched on before
    the transmitter — which means transmitter trim needs no setting anywhere.
    A reading that spreads while it is measured, or that lands more than
    `RCL_CENTER_MAX_SHIFT_US` from the configured centre, is discarded rather
    than used, so a stick held at power-up cannot be calibrated as neutral.
    Both sticks read as centred until their measurement is done.
  - A failsafe that flashes the hazards when a channel it needs goes quiet, and
    recovers on its own when the receiver comes back. Channel 3 is exempt while
    it is configured off, so a two-channel receiver is not a fault.
  - `rcl_config_validate()`, called by `rcl_init()`, so a configuration that
    cannot be acted on is refused rather than acted on badly.

- **The Arduino layer** (`src/RcLights.h`, `src/RcLights.cpp`). Pulse capture,
  PWM output, and nothing else.
  - Interrupt-driven capture on three channels. On AVR it brings its own
    pin-change interrupt handlers, because an ATmega328P has two external
    interrupt pins and three channels do not fit on two; `RCLIGHTS_NO_AVR_PCINT`
    falls back to `attachInterrupt()` for sketches that need those vectors for
    something else.
  - PWM through `analogWrite()`, or through LEDC on ESP32 cores before 3.0.
  - **`RcLights` derives from `rcl_config_t`**, so every setting the controller
    has is a field of the object, spelled the same way: `lights.esc_mode = ...`,
    `lights.level_rear_brake = ...`, `lights.cal[RCL_CH_STEER].invert = ...`.
    The constructor fills them from `rcl_config_default()`, `begin()` reads
    them, and `apply()` puts a change into force while running. There is no
    second list of settings in the wrapper to fall out of step with the first:
    a field added to `rcl_config_t` is a field of `RcLights` at the same moment.
  - One setting that is *not* in there: `outputs`, picking LED polarity
    (`RCLIGHTS_OUTPUTS_ACTIVE_HIGH` or `_ACTIVE_LOW`), because that is a fact
    about the wiring rather than about the controller. It is applied before the
    pins are first driven, so an active-low string does not flash at full
    brightness on the way up.
  - `begin(pins, cfg)` and `applyConfig(cfg)` remain for a sketch that keeps
    its settings in an `rcl_config_t` of its own; both replace the object's
    fields with what was applied, so it never reports a setting it is not
    using.
  - Per-output inversion for a car wired both ways round, unconnected outputs
    that are simply not driven, and a single-instance rule that refuses a second
    `begin()` rather than quietly stealing the first one's interrupts.
  - `captureCenter()` for taking the transmitter's rest position as the centre
    of the steering and throttle channels.

- **Board support** (`src/rclights_board.h`). Default pin maps for AVR, ESP32
  and STM32, each chosen so every input can raise an interrupt and every output
  has a timer channel behind it, avoiding the strapping and flash pins on ESP32
  and the user LED on Nucleo. Each pin is its own guarded macro, so a single
  one can be moved with a build flag (`-DRCLIGHTS_PIN_FRONT=6`) or a `#define`
  in the sketch, without editing the library.

- **Tests** (`tests/`). Three host suites, 288 checks: `test_core.c` drives the
  controller through stick sequences with no board in the way,
  `test_arduino_port.cpp` drives the real wrapper against a mock Arduino runtime
  that delivers pulses as edges on pins, and `test_example_defaults.cpp`
  compiles the shipping `RcLightsCar.ino` and fails if the settings its optional
  block lists ever stop matching `rcl_config_default()` — at which point the
  block would silently be a set of overrides rather than documentation.

- **Examples**: `RcLightsCar`, a complete car asking only for the LED polarity,
  the ESC behaviour and the kind of switch on channel 3 — the pins come from the
  board and the stick centres measure themselves — followed by an optional block
  listing every other setting at the value it already has, to be deleted or
  edited a line at a time; plus `RcLightsBasic` (the library in fifteen lines),
  `RcLightsSwitchModes` (the four things channel 3 can be) and
  `RcLightsCalibrate` (measure your receiver, and watch what the controller
  makes of it).

- **CI** (`.gitlab-ci.yml`, `tools/ci.sh`): formatting, clang-tidy,
  arduino-lint, version consistency, the host suites with and without the
  sanitizers, a Doxygen run that fails on an undocumented declaration, and the
  examples compiled for Nano, ESP32 DevKit and Nucleo-64 — plus a separate job
  for the AVR fallback path, which no other job compiles.

### Not verified

The library compiles for all three targets and passes its host suites. It has
**not** been run on a car. In particular, `coast_ms` — how long the car is
assumed to keep rolling after you lift off — is the one setting that cannot be
guessed from a desk, and is what decides whether a braking manoeuvre ever shows
the reversing light.
