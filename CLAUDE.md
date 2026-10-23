# Fernotron Matter bridge

ESP32-S3 firmware (M5Stack AtomS3 Lite) that exposes Rademacher Fernotron
shutters to Alexa as Matter Window Coverings, transmitting on 433.92 MHz
through an M5Stack Unit RF433T. The build plan is in docs/plan.md; where this
repo deliberately differs from it, docs/decisions.md says so and why.

The house: 8 shutters in 4 rooms, one 2411 group per room (1-4), members 1-2.

## Commands
Every shell first needs `source tools/env.sh` (ESP-IDF + esp-matter).
- Toolchain install: `tools/setup_toolchain.sh` (pinned versions, ~/esp)
- Host tests:   `tools/host_test.sh`
- Device build: `idf.py build`
- Flash + log:  `idf.py -p /dev/cu.usbmodem* flash monitor`
- Erase flash:  `idf.py -p /dev/cu.usbmodem* erase-flash`
A change is done when host tests pass and the device build succeeds.

## Device configuration
Names, 2411 addresses, travel times and polarity flags live in NVS
(components/config, main/settings) and change at runtime over the serial
console, no rebuild: `matter esp shutter`, `matter esp house`, `matter esp cal <n> up|down`
then `matter esp cal done`. Shutter slots are fixed: slot i is always the i-th
cover endpoint, so never reorder or delete slots.

## Hardware facts
- RF433T data is GPIO2 (Grove yellow). HIGH means carrier on.
- Drive GPIO2 LOW first thing in app_main(); RMT end-of-transmission level is LOW.
- RMT resolution is 400 kHz (2.5 us): 400 us = 160 ticks, max duration 81.9 ms.
- Front button is GPIO41, active LOW.
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
- All transmissions go through main/rf_tx, which enforces the 10% budget
  per sliding hour (fernotron::Airtime). Never bypass or loosen it.
- Hand test without Alexa: `matter esp send <group> <member> up|down|stop`.

## Architecture rules
- components/fernotron, components/shutter and components/config are pure
  C++17: no ESP-IDF headers. They build on the host in test/.
- main/ holds the adapters: Matter, RMT, NVS, CLI, diag.
- Matter callbacks never block: enqueue an event and return.
- Log every received Matter command to diag: cluster, command, value, fabric.
- Endpoint IDs never change for an existing shutter.
- Use the esp-matter, RMT and NVS APIs; never reimplement their internals.

## Testing
- New component code comes with Unity tests under test/.
- Encoder output must match test/vectors bit for bit.

## Style
- Comments say what the code does and why. No historical notes such as
  "replaces X" or "we used to have Y".
- Logic belongs in components/; keep main/ files small.
