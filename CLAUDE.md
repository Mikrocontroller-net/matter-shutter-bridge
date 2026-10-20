# Fernotron Matter bridge

ESP32-S3 firmware (M5Stack AtomS3 Lite) that exposes Rademacher Fernotron
shutters to Alexa as Matter Window Coverings, transmitting on 433.92 MHz
through an M5Stack Unit RF433T. The build plan is in docs/plan.md.

## Commands
- Device build: `pio run -e atoms3`
- Host tests:   `pio test -e native`
- Flash:        `pio run -e atoms3 -t upload`
- Erase flash:  `pio run -e atoms3 -t erase`
- Serial log:   `pio device monitor -e atoms3`
A change is done when host tests pass and the device build succeeds.

## Hardware facts
- RF433T data is GPIO2 (Grove yellow). HIGH means carrier on.
- Drive GPIO2 LOW first thing in setup(); RMT end-of-transmission level is LOW.
- RMT tick is 400 kHz (2.5 us): 400 us = 160 ticks, max duration 81.9 ms.
- Matter needs the Huge APP partition scheme and a full flash erase on upload.
- Download mode: hold the reset button about 2 s until the green LED lights.

## Fernotron rules
- Address = 2411 sender ID + group + member (1-7). 0 means all.
- Never send set or end-position commands to group or member 0; use a
  motor code (9 + the code on the motor label), which reaches one motor.
- One transmission at a time. The counter increments per new command and
  stays identical across that command's repeats.

## Radio rules
- EU 433 MHz band: 10 mW e.r.p., under 10% duty cycle. Never add power.
- Repeats are bounded (default 4); prefer group addressing to loops.
- Never transmit in a loop on hardware without a delay and a repeat limit.

## Architecture rules
- lib/ is pure C++17: no Arduino.h, no ESP-IDF headers.
- Matter callbacks never block: enqueue an event and return.
- Log every received Matter command to diag: cluster, command, value, fabric.
- Endpoint IDs never change for an existing shutter.
- Use the Matter and RMT library APIs; never reimplement their internals.

## Testing
- New lib/ code comes with Unity tests under test/.
- Encoder output must match test/vectors bit for bit.

## Style
- Comments say what the code does and why. No historical notes such as
  "replaces X" or "we used to have Y".
- Logic belongs in lib/; keep src/ files small.
