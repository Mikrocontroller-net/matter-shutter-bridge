# Decisions

Where this repo deliberately differs from `docs/plan.md`, and why. Newest first.

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
