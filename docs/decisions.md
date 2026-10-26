# Decisions

The main design decisions and why they were made. Newest first.

## 2026-10-26 — USB console waits for a USB host

**Decision.** linenoise reads the console through a function of ours that
waits until a USB host is connected (`linenoiseSetReadFunction`).

**Why.** In ESP-IDF 6 a read from the USB-Serial-JTAG console fails at once
while no host is connected (IDF-14303). On a plain power supply the Matter
SDK's shell loop then ran without pause, printing an empty line per pass,
which also showed up in `/console` replies.

## 2026-10-26 — Whole frames in RMT memory, deferred position writes

**Decision.** The radio's RMT channel takes three of the S3's four TX memory
blocks (144 symbols), so a whole frame (141 symbols and the end marker) is in
place before it starts and nothing is refilled while it is on air. The two
current-position attributes of every cover are written to flash 3 s after
their last change instead of on every change.

**Why.** A frame used to need a refill from an interrupt halfway through.
That interrupt cannot run while flash is written, and an NVS page erase
blocks for tens of milliseconds, long enough to corrupt the copy on air.
esp-matter wrote both position attributes to flash on every update, every
500 ms per moving shutter: about 30 writes a second in a routine, with a page
erase every few seconds. Commands failed mostly while shutters moved, in
routines and reversals; a corrupted copy fails the motor's checksum and is
dropped. The bridge keeps its own copy of each position (`positions` in NVS)
when a move ends, so the deferred attribute only matters for the controllers'
view after a restart.

**Cost.** No RMT TX block is left for anything beyond the status LED. A
frame with a longer preamble than 9 would no longer fit and would be refilled
again.

## 2026-10-25 — Updates, console and log over Wi-Fi

**Decision.** The stick serves three HTTP endpoints on port 80 (main/remote):
`/ota` takes an app image and restarts into it, `/console` runs one console
line and returns its output, `/log` returns log text from a 16 KB ring. Each request needs the stick's own 128-bit token, drawn at first
start, kept in NVS and shown only on the USB console. The bootloader's
rollback is enabled: new firmware must reach Wi-Fi with the server running
within 60 s, and a restart or crash before that returns to the previous one.
`tools/remote.sh` is the client. The DHCP host name is `shutter-bridge`.

**Why.** On the desk, USB is convenient; but radio range problems only show
where the stick will live. Matter's own OTA needs an OTA provider: a
controller that fetches certified images from the DCL, or a separate provider
app joined to the fabric. A direct upload needs neither. Console lines run on
the Matter task, as USB console lines do, with stdout pointed at a memory
buffer, so both consoles share one set of commands and never run at once.
The Matter SDK prints each log line in three parts, two of them with printf;
a log redirect makes it one esp_log call, so the ring gets whole lines.
Only the SDK's errors go to the ring: its other lines (message traces,
attribute reports at ~15 a second while shutters move) flushed the 16 KB
within seconds, so they go to USB only. The ring then keeps the bridge's own
lines, radio frames included, for hours.

**Cost.** Plain HTTP and unsigned images: someone who can read the home
network's traffic can take the token and install firmware. 16 KB of RAM for
the log ring. The SDK's own shell commands (`matter config` and others) write
straight to USB and are not captured. The rollback bootloader needs one USB
flash.

## 2026-10-24 — Interleaved copies, no repeat at the end of travel

**Decision.** `rf_tx` sends the copies of all commands on air in turn, one
copy per command (`fernotron::Interleaver`), instead of each command's copies
back to back. A STOP goes next, and a newer command drops the remaining
copies of older ones for an overlapping address. The controller no longer
repeats an open/close command when the shutter should have reached its end.

**Why.** In a routine closing every room, the last room's first copy left
~5.7 s after the first room's, and a bathroom shutter that missed its 4
back-to-back copies only moved on the repeat at the end of travel, ~21 s
later. Interleaved, every room gets a copy within about one round (~1.2 s
for four rooms) and each command's copies spread over several seconds, so a
short burst of interference rarely hits all of them. The repeat at the end of
travel came too late to help and cost as much airtime as a whole command.

**Cost.** A single command's copies still go out back to back, so a shutter
that misses all of them is not retried.

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
frame to (0, 0). STOPs go first.

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
the next counter value and goes out `house repeats` times with that counter,
interleaved with the copies of other commands (2026-10-24). Every frame, lead-in
included, is sent as tronferno does.

Before each frame, `fernotron::Airtime` books its airtime against 360 s per
sliding hour (10 %); when the budget is used up the copy is dropped and
logged. Airtime counts the whole frame up to its last pulse, not
just carrier-on time, which is the conservative reading of the duty-cycle
rule. One command with 4 repeats costs about 1 s, so roughly 370 commands an
hour fit.

Real motors accept these frames since 2026-10-23; they have not been compared
bit for bit with a 2411 capture.

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
