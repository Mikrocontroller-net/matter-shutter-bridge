# Decisions

Where this repo deliberately differs from `docs/plan.md`, and why. Newest first.

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
runtime and take effect immediately; `matter cal <n> up|down` + `matter cal
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
  Espressif's primary Matter path. The plan already named it as the escape
  hatch; `components/fernotron` and `components/shutter` moved over unchanged.

**Cost.** A multi-GB toolchain install and a slow first build; more verbose
Matter code in `main/`.

## 2026-10-20 — Spike exposes 8 bridged covers by default

**Decision.** The spike creates an Aggregator with one bridged Window Covering
per shutter, named from firmware ("Wohnzimmer links"). A build option switches
to standalone endpoints so both layouts can be compared during the spike;
the final choice is made at gate G0, before anything is paired for real.

**Why.** 8 shutters, plus optional room covers, is past the point where the
plan suggests a bridge. Bridged nodes carry their own names; how Alexa names
8 identical standalone endpoints on one node is unverified.

## 2026-10-20 — Fernotron addressing for this house

Each room is a 2411 group (1–4) with two members (1–2). A room-wide command is
one frame to group *g*, member 0, which reaches both shutters at once.

## 2026-10-20 — Protocol details settled from reference code

`docs/plan.md` marked the message layout as approximate. Reading tronferno-lib
(MIT licence, not GPL as assumed) and FernoPy's URH decoder settled it; see
`components/fernotron/src/fernotron/message.h` and `encoder.h`. Corrections to
the plan:

- The gap before each 10-bit word is 9 bit times (400 µs on, 3200 µs off),
  not 8.
- Each byte is sent twice; the 2 parity bits depend on whether it is the first
  or second copy.
- A frame is 141 RMT symbols with the optional 25 ms lead-in.

Still open until captures of our own 2411 exist: tronferno sends 7 preamble
periods, FernoPy's captures show 6 (configurable in `Timing`), and whether the
lead-in helps or hurts.
