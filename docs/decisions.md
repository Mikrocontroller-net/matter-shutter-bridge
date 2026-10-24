# Decisions

The main design decisions and why they were made. Newest first.

## 2026-10-24 — Per-device setup code, generated on the device

**Decision.** At its first start each stick draws a random setup passcode,
discriminator and SPAKE2+ salt (hardware RNG with its bootloader entropy
source enabled, since Wi-Fi and Bluetooth are not running yet), stores them in
NVS (`setup_code`, components/pairing) and serves them to esp-matter as a
custom commissionable data provider. The QR code and 11-digit code are printed
at boot and by `matter esp pairing`. The SPAKE2+ verifier is derived when a
pairing window first opens.

**Why.** The SDK's test code 20202021 is public, so anyone in Bluetooth range
or on the network could pair a stick whose window is open. Espressif's
manufacturing tool would also give each device its own code, but it is built
for production runs: a factory partition generated on a computer per device
and flashed separately. Generating on the device needs no extra step.

**Cost.** The code lives only on the stick and its console output; there is
no printed label. A factory reset keeps it; erasing the flash draws a new one.

## 2026-10-24 — Pairing window and reset on the button, status on the LED

**Decision.** Holding the button 3 s opens a pairing window for 15 minutes,
10 s factory-resets; both happen on release, and the LED shows beforehand
which one a release will trigger. The window is advertised over Wi-Fi only
(DNS-SD). The LED otherwise shows status: radio frames, pairing window, no
Wi-Fi, airtime above 80 %, idle.

**Why.** A commissioned stick opens no pairing window by itself, so a second
controller needed Alexa's share flow. Bluetooth is released after the first
pairing, which leaves Wi-Fi for the button's window. The reset moved from
5 s (immediate) to 10 s (on release) so it cannot be hit while aiming for
pairing. The SDK posts no events for the pairing window here, so the LED task
polls its state on the Matter task every 500 ms.

## 2026-10-24 — Report exact positions, kept across restarts

**Decision.** Positions are reported exactly (0 = open, 10000 = closed). They
are saved to NVS whenever a move ends and reported from boot on.

**Why.** Alexa does not send "open" to a cover it believes is fully open
(observed: a routine opening all shutters sent nothing after a restart had
reset every estimate to open). Two workarounds failed with Apple Home:
reporting the ends one hundredth short of exact (0.01 % / 99.99 %) left it
showing "opening"/"closing" because a commanded end was never reached, and a
null (unknown) position after a restart made it show every cover as not
responding. Keeping the position across restarts removes the cause Alexa
tripped over.

**Cost.** Presses on a physical remote still go unseen; until the next full
open or close through Matter, Alexa may skip a command it believes is done.

## 2026-10-23 — Merge commands for rooms and the whole house

**Decision.** `rf_tx` collects commands in `fernotron::Batcher` until 300 ms
pass without a new one, at most 2 s after the first.
When every configured motor of a group got the same command it sends one
frame to (group, member 0); when every configured group is complete, one
frame to (0, 0). STOPs go first, and merges are recomputed after every frame.

**Why.** Alexa sends one Matter command per shutter for a room or group. At
~1.2 s per command with 4 repeats, "all shutters" took ~9 s from first to
last, and a timed STOP queued behind other commands could stop a shutter
seconds late. Merged, the whole house moves with one ~1.2 s command.

A fixed 150 ms window after the first command was not enough: Alexa sends a
routine's commands about 200 ms apart (8 commands over 1.3 s), so they left as
six frames and the last room started moving ~6 s after the first.

**Cost.** 300 ms latency for a single command, also for timed STOPs (~1.7 %
of an 18 s travel); up to 2 s for a long burst. Group frames reach every motor paired to that group on the
2411, so the shutter table must list all of them.

## 2026-10-23 — Radio: one TX task, airtime counted per frame

**Decision.** `main/rf_tx` owns the RMT channel (GPIO2, 400 kHz, idle and
end-of-transmission LOW) and the Fernotron counter. A command is queued, gets
the next counter value and goes out `house repeats` times with that counter;
each frame's 42.8 ms trailing gap separates the repeats. Every frame, lead-in
included, is sent as tronferno does.

Before each frame, `fernotron::Airtime` books its airtime against 360 s per
sliding hour (10 %); when the budget is used up the rest of that command is
dropped and logged. Airtime counts the whole frame up to its last pulse, not
just carrier-on time, which is the conservative reading of the duty-cycle
rule. One command with 4 repeats costs about 1 s, so roughly 370 commands an
hour fit.

**Unverified.** Nothing has been transmitted yet; frames have not been
compared with a 2411 capture (gate G1).

## 2026-10-23 — Configuration lives in NVS, edited over the serial console

**Decision.** The 2411 ID, repeat count and per-shutter name, group, member,
travel times and polarity flags are one versioned blob in NVS
(`components/config`, `main/settings.cpp`). Console commands change them at
runtime and take effect immediately; `matter esp cal <n> up|down` + `matter esp cal
done` measures travel times with a stopwatch.

**Why.** Calibration happens at the window, not at the compiler, and the
polarity flags must be switchable the night Alexa changes behaviour.

**Limits for now.** Slots are fixed (8 from the defaults); adding or removing
shutters needs dynamic endpoints and comes later. A bridged shutter's unique ID
is tied to its slot, not its address, so renaming or re-addressing keeps it the
same device in Alexa. Whether Alexa picks up a NodeLabel changed after pairing
is unverified. Possible later additions: a vendor-specific Matter cluster for
times and flags (writable with chip-tool), and a JSON file pushed over serial.

## 2026-10-20 — ESP-IDF + esp-matter instead of Arduino-ESP32

**Decision.** Device firmware is a plain ESP-IDF project on ESP-IDF v6.0.2 with
esp-matter `release/v1.6.1` (commit `4c9dd7aa`), the pairing esp-matter
recommends. `tools/setup_toolchain.sh` installs exactly these. PlatformIO is no
longer used; host tests are a small CMake project (`test/`) using the Unity copy
inside ESP-IDF.

**Why.**
- The house has 8 shutters (4 rooms × 2). Arduino-ESP32 3.3.9 ships esp-matter
  precompiled with `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=4`, and the
  root endpoint counts, so at most 3 covers.
- Raising it needs pioarduino's hybrid compile, which failed four times on
  tooling, not on our code: spaces in the project path, the partition table
  lookup, an SCons version clash between PlatformIO Core 6.2.0 and pioarduino,
  and a bundled esp-modbus config assertion.
- The Arduino `MatterWindowCovering` class reports whole percent, infers
  StopMotion from "target equals current", hides the fabric index (which
  `CLAUDE.md` requires in diag) and has no bridge support.
- esp-matter exposes all of that directly, has bridge examples, and is
  Espressif's primary Matter path. `components/fernotron` and
  `components/shutter` moved over unchanged.

**Cost.** A multi-GB toolchain install and a slow first build; more verbose
Matter code in `main/`.

## 2026-10-20 — Bridged covers, not standalone endpoints

**Decision.** The firmware creates an Aggregator with one bridged Window
Covering per shutter, named from the configuration. Until 2026-10-23 a build
option could switch to standalone endpoints for comparison; it was removed
once Alexa had paired the bridge and moved a real shutter through it.

**Why.** Bridged nodes carry their own names; Alexa listed all 8 by the
names the firmware gave them.

## 2026-10-20 — Fernotron addressing for this house

Each room is a 2411 group (1–4) with two members (1–2). A room-wide command is
one frame to group *g*, member 0, which reaches both shutters at once.

## 2026-10-20 — Protocol details settled from reference code

The original plan marked the message layout as approximate. Reading tronferno-lib
(MIT licence, not GPL as assumed) and FernoPy's URH decoder settled it; see
`components/fernotron/src/fernotron/message.h` and `encoder.h`. Corrections to
the original assumptions:

- The gap before each 10-bit word is 9 bit times (400 µs on, 3200 µs off),
  not 8.
- Each byte is sent twice; the 2 parity bits depend on whether it is the first
  or second copy.
- A frame is 141 RMT symbols with the optional 25 ms lead-in.

The defaults (7 preamble periods, 25 ms lead-in) moved a real motor on
2026-10-23. Whether FernoPy's 6 periods or no lead-in
also work is untested; it only matters for airtime.
