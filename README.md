# matter-shutter-bridge

Control Rademacher **Fernotron** roller shutters with **Matter** controllers
such as Apple Home or Alexa through a small ESP32 stick, without replacing
motors, re-pairing receivers or running a home server.

matter-shutter-bridge is firmware for an M5Stack AtomS3 Lite with a 433 MHz transmitter.
It appears as a Matter bridge with one window covering per shutter, and
translates Matter commands into the same radio frames your Fernotron
central unit (2411) sends. Your shutters cannot tell the difference, and your
existing remotes keep working.

> **Status: working.** In daily use with Alexa and Apple Home on 8 shutters.
> It uses Matter **test certificates**, so the apps show it as an
> uncertified device. Treat it as a hobby project, not a finished product;
> see [Limitations](#limitations).

## What you get

- Every shutter as its own device in your smart home app, with rooms,
  groups, sliders and voice control (see [Compatibility](#compatibility)).
- Open, close, stop and **go to a percentage**. Positions are estimated from
  each shutter's measured travel time, since Fernotron is one-way radio.
- Whole rooms or the whole house move together: commands for several
  shutters are merged into one radio command per room, or one for everything.
- Configuration over USB (names, addresses, travel times) without rebuilding
  the firmware.
- Firmware updates, console and log over Wi-Fi, so the stick can sit where
  the radio reaches best.
- Local control: your Echo or iPhone talks to the stick on your LAN; no cloud
  skill, no extra hub.

## What you need

### Hardware

| Part | Notes |
| --- | --- |
| [M5Stack AtomS3 Lite](https://docs.m5stack.com/en/core/AtomS3%20Lite) | ESP32-S3 with 8 MB flash, USB-C, Grove port. About €10. |
| [M5Stack Unit RF433T](https://docs.m5stack.com/en/unit/rf433_t) | 433.92 MHz ASK transmitter. Plugs into the Grove port, data on GPIO2. About €5. |
| USB-C power supply | Any phone charger. |

No soldering. Place the stick where your 2411 remote reaches all shutters;
433 MHz range depends far more on placement than anything else.

### Your Fernotron installation

- A **Rademacher 2411 central unit** that your shutters are already paired
  to. The bridge sends as that 2411, so nothing has to be re-paired.
- The 2411's **ID**: the 6-character code starting with `80` on the sticker
  in its battery compartment (e.g. `80a1b2`).
- Which **group and member** each shutter has on the 2411 (groups 1–7,
  members 1–7).

### Smart home side

- A supported **Matter controller**, see [Compatibility](#compatibility).
- **2.4 GHz Wi-Fi** on the same network as the controller, with multicast/mDNS
  not blocked between devices (no client isolation).

### On your computer

macOS or Linux with about 15 GB free disk space for the ESP-IDF and Matter
SDK toolchain, plus `git`, `cmake`, `ninja` and Python 3 (3.13 is what the
setup script prefers; the newest Python is not supported by the Matter
tooling yet).

## Getting started

### 1. Install the toolchain (once)

```bash
tools/setup_toolchain.sh
```

This installs the pinned versions of ESP-IDF (v6.0.2) and esp-matter
(release/v1.6.1) into `~/esp`. It downloads several GB and takes a while.
Every new terminal then needs:

```bash
source tools/env.sh
```

### 2. Set up your shutters (optional, before the first flash)

The first boot creates a default table of **8 shutters: 4 groups × 2
members**, named "Raum 1 links" … "Raum 4 rechts". Names, addresses and
times can be changed later over USB. **The number of shutters can't yet**: if
yours differs, edit `defaults()` in
[`components/config/src/config/house.cpp`](components/config/src/config/house.cpp)
before the first flash.

Alexa remembers each shutter by its slot, so pick the order once and keep it.

### 3. Build and flash

Connect the AtomS3 over USB, then:

```bash
idf.py build
idf.py -p /dev/cu.usbmodem* erase-flash flash monitor
```

On Linux the port is usually `/dev/ttyACM0`. If flashing doesn't start, hold
the reset button for about 2 s until the green LED lights, then retry. The
monitor shows the log and accepts console commands; quit it with `Ctrl+]`.
Later updates can go over Wi-Fi (see
[Updates and console over Wi-Fi](#updates-and-console-over-wi-fi)).

### 4. Pair with Alexa

1. Read the stick's pairing code from the serial monitor. Each stick draws its
   own code at its first start and keeps it; it is printed at every start and
   again with `matter esp pairing`:
   ```
   Pairing code: 1234-567-8901
   QR code:      MT:...
   As image:     https://project-chip.github.io/connectedhomeip/qrcode.html?data=...
   ```
2. In the Alexa app: **+ → Add device → Matter**, then scan the QR code (open
   the "As image" link) or enter the 11-digit pairing code.
3. Accept the warning about an uncertified device.

Alexa then finds a hub named "Rollladen-Bridge" and one device per shutter.

An unpaired stick keeps its pairing window open for about 15 minutes after
each start; replug it if the window has closed.

To add a second controller such as Apple Home later, either share the bridge
from the Alexa app (device settings of the hub, "other assistants and apps")
and enter the code Alexa shows, or hold the stick's button for 3 seconds
until the LED turns blue and let go. The pairing window then opens for 15
minutes and the other app pairs with the stick's code. This second way works
over Wi-Fi only, so the phone must be on the same network.

Keep the pairing code to yourself: while the pairing window is open, anyone
within Bluetooth range (unpaired stick) or on your network (button) who knows
it could pair the stick. A factory reset keeps the code; flashing with
`erase-flash` draws a new one.

### 5. Tell it your 2411 and shutters

In the serial monitor:

```text
matter esp house id 80a1b2          # your 2411's ID from the sticker
matter esp shutter                  # list shutters with group/member
matter esp shutter 3 address 2 1    # fix one shutter's group/member
matter esp shutter 1 name Schlafzimmer links
```

**From the moment the ID is set, commands move real shutters.** Test one
first:

```text
matter esp send 1 1 up      # group 1, member 1
matter esp send 1 1 stop
```

### 6. Calibrate travel times

Percentages are only accurate with each shutter's real travel time. With the
shutter fully open:

```text
matter esp cal 1 down       # sends DOWN and starts a stopwatch
matter esp cal done         # type this the moment the shutter stops
```

Then the same with `cal 1 up` from fully closed. Repeat for every shutter.

## Console reference

All commands are typed into the serial monitor and start with `matter esp`.
Shutters are numbered from 1 in the order of `matter esp shutter`. Changes
are saved immediately and survive reboots and firmware updates.

| Command | Does |
| --- | --- |
| `shutter` | List all shutters: endpoint, name, group, member, travel times, polarity flags |
| `shutter <n> name <text>` | Rename (up to 32 bytes, umlauts allowed) |
| `shutter <n> address <group> <member>` | Set 2411 group and member (1–7) |
| `shutter <n> times <up_ms> <down_ms>` | Set travel times in milliseconds |
| `shutter <n> invert ends\|mid on\|off` | Swap open/close (`ends`) or flip percentages (`mid`), in case Alexa's direction is reversed |
| `house` | Show 2411 ID and repeat count |
| `house id <hex>` | Set the 2411 ID |
| `house repeats <n>` | Radio repeats per command (1–8, default 4) |
| `cal <n> up\|down` / `cal done` / `cal cancel` | Measure a travel time |
| `send <group> <member> up\|down\|stop` | Send one command directly; 0 means all groups or all members |
| `diag` / `diag clear` | List the commands received from Alexa, with values |
| `status` | Estimated position and motion of each shutter |
| `pairing` | This stick's pairing code and QR code |
| `remote` | Address and access token for updates over Wi-Fi |

## Updates and console over Wi-Fi

Once the stick is on Wi-Fi it can live where the radio reaches best:
firmware updates, console commands and the log also work over the network.
`matter esp remote` on the USB console shows the stick's address and its own
access token. Put both into `.bridge-remote` in the repository root (Git
ignores it):

```bash
BRIDGE_HOST=192.168.1.23    # or shutter-bridge, if your router resolves DHCP names
BRIDGE_TOKEN=0123456789abcdef0123456789abcdef
```

| Command | Does |
| --- | --- |
| `tools/remote.sh flash` | Build, upload, restart into the new firmware and check that it runs |
| `tools/remote.sh cmd matter esp status` | Run one `matter esp` command and print its output |
| `tools/remote.sh log -f` | Print the kept log (the bridge's own lines, radio frames included, and Matter errors) and keep following it |

New firmware is on probation: if it crashes or doesn't reach Wi-Fi within
60 s, the stick goes back to the previous one. The bootloader that does this
comes with the first USB flash of this version. Settings, pairings and codes
stay through updates.

The connection is plain HTTP and images are not signed, so someone who can
read your home network's traffic could pick up the token. Use it only on a
network you trust.

## Button and LED

The button is the stick's whole top. What a hold does is decided when you let
go, and the LED shows it beforehand:

| Hold | LED while holding | When you let go |
| --- | --- | --- |
| under 3 s | dim white | nothing |
| 3–10 s | blue | the pairing window opens for 15 minutes |
| 10 s or more | blinking red | factory reset: Matter pairing and Wi-Fi are erased and the stick restarts unpaired; the 2411 ID and the shutter table stay |

The rest of the time the LED shows the stick's state:

| LED | Meaning |
| --- | --- |
| dim green | connected and idle |
| white flash | a radio frame going out (one per repeat) |
| pulsing blue | pairing window open |
| orange | no Wi-Fi |
| red | more than 80 % of the hourly radio budget used |

## Compatibility

| Controller | App | Voice | Routines / automations |
| --- | --- | --- | --- |
| Apple Home | Yes | Yes (Siri) | Not tested (needs a home hub) |
| Amazon Alexa | Yes | Only reliable through routines with a custom phrase | Yes |

Without an Apple home hub (Apple TV or HomePod), iOS only connects while the
Home app is open. Google Home and Home Assistant are untested.

## Using it with Alexa

- Rename shutters and assign rooms in the Alexa app as with any device. Alexa
  takes the names from the stick when pairing; a name changed on the stick
  later may not show up in Alexa, so rename in the app.
- For voice control, create a routine with your own phrase, for example
  "Alexa, Rollos im Schlafzimmer auf", that opens the shutters.
- Percentages in the Alexa app mean "percent closed", the Matter convention.
- The shutters can be used as actions in Alexa routines.

## Limitations

- **Uncertified test credentials.** Fine for personal use; not something to
  sell or hand out as is.
- **Estimated positions.** Fernotron receivers don't report their position.
  The stick estimates it from travel times, keeps it across restarts and
  resyncs on every full open or close. Presses on physical remotes are not
  seen, so the estimate can be off until the next full open or close; Alexa
  may then skip "open" or "close" for a shutter it believes is already there.
- **Fixed number of shutters** after the first boot (8 by default, up to 16 in
  the code). Adding or removing shutters at runtime is not supported yet.
- **The shutter table must list every motor of each group used.** Room and
  whole-house commands are sent as one frame to the group (or to all groups),
  which also reaches motors paired to that group but missing from the table.
- **Central-unit addressing only.** Motor codes, set mode, end positions and
  timers of the 2411 are not used.
- **Alexa's Matter cover support changes.** In 2026 Alexa has reversed
  open/close and dropped cover commands at times; `invert ends|mid` and the
  `diag` log exist for that.

## Radio regulations

The bridge transmits on 433.92 MHz in the EU short-range-device band. The
RF433T is a 10 mW transmitter, which is already the legal limit, and the
firmware refuses to exceed a 10 % duty cycle over any hour. Don't add power
amplifiers or bigger transmitters. You are responsible for operating it
legally where you live.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `Error: 47` after a console command | Missing `esp` prefix: it's `matter esp house`, not `matter house`. |
| Alexa can't find the device | Pairing window closed (replug, or hold the button for 3 s), phone not on 2.4 GHz or Bluetooth off, or the stick is too far from the phone. |
| "Gerät reagiert nicht" (device not responding) | Weak Wi-Fi at the stick (check `rssi` in the boot log) or multicast filtering in the router or mesh. |
| Command in `diag`, shutter doesn't move | 2411 ID not set or wrong, wrong group/member, or out of radio range. |
| Voice does nothing, app works | Alexa didn't map the sentence to the device; nothing arrives in `diag`. Try the device or room name with "öffne"/"schließe". |
| Shutter stops at the wrong percentage | Travel times not calibrated, or position estimate out of sync: open or close it fully once. |

## How it works

```
Alexa (Echo) ──Matter over Wi-Fi──▶ AtomS3 Lite ──433 MHz──▶ Fernotron receivers
                                    │
                                    ├─ Matter bridge: one Window Covering per shutter
                                    ├─ shutter controller: target → UP/DOWN/STOP + timed STOP
                                    ├─ batcher: merges commands per room / house, STOPs first
                                    └─ encoder + RMT: Fernotron frames, 400 µs bit timing
```

A Matter command such as "go to 30 % closed" sets the cover's target. The
shutter controller compares it with the estimated position, sends UP or DOWN,
and schedules a STOP after the computed travel time. Fully open and fully
closed simply run into the motor's end stop. Commands that arrive less than
1 s apart are collected and merged when a whole room or the whole house gets
the same command. Each command goes out several times with the same counter,
back to back, and the next command waits for 2 s of silence: Fernotron
receivers ignore a frame that follows another one closely.
The encoder builds the Fernotron message (2411 ID, group, member, command,
counter, checksum) and the RMT peripheral sends it with hardware timing.

### Repository layout

| Path | Contents |
| --- | --- |
| `main/` | ESP-IDF adapters: Matter bridge (`covers`), radio (`rf_tx`), button and LED (`panel_hw`), Wi-Fi access (`remote`), settings in NVS, console, command log |
| `components/fernotron/` | Fernotron message builder, frame encoder, duty-cycle guard, command batcher |
| `components/shutter/` | Position estimator and command controller |
| `components/config/` | Configuration model, storage format and console command parser |
| `components/panel/` | What the LED shows and what button holds do |
| `components/pairing/` | Per-device setup code: generation and storage format |
| `components/remote/` | Log ring and access token for updates over Wi-Fi |
| `test/` | Host unit tests (Unity) for the six components |
| `tools/` | Toolchain setup, environment, host tests, updates over Wi-Fi |
| `docs/` | Design decisions |

The six components are plain C++17 without ESP-IDF headers, so most logic
is tested on the computer:

```bash
tools/host_test.sh
```

[`docs/decisions.md`](docs/decisions.md) explains the main design choices
(ESP-IDF instead of Arduino, bridge layout, merging, position reporting).

## Acknowledgements

This project stands on the work of others:

- **[tronferno-mcu](https://github.com/zwiebert/tronferno-mcu)** and
  **[tronferno-lib](https://github.com/zwiebert/tronferno-lib)** by
  zwiebert: the most complete open implementation of the Fernotron protocol.
  The message layout, parity scheme and transmit timings here follow
  tronferno-lib (MIT), and its tests provided the first reference frame.
  tronferno-mcu (GPL-3.0) is the full-featured alternative if you want 2411
  timers, sun automation or receiving.
- **[FernoPy](https://github.com/prefixFelix/FernoPy)** by prefixFelix:
  protocol notes and a Universal Radio Hacker decoder that confirmed the
  on-air framing.
- **[esp-matter](https://github.com/espressif/esp-matter)** and
  **[ESP-IDF](https://github.com/espressif/esp-idf)** by Espressif, and the
  **[Matter SDK](https://github.com/project-chip/connectedhomeip)** by the
  Connectivity Standards Alliance and contributors (all Apache-2.0).
- **[Home Assistant Matter Hub](https://github.com/RiDDiX/home-assistant-matter-hub)**
  by RiDDiX and its community: their documentation of how Alexa handles Matter
  covers shaped the polarity flags and command logging.
- **[Universal Radio Hacker](https://github.com/jopohl/urh)** for capturing
  and analysing 433 MHz signals.

No code was copied from FernoPy, which has no licence file. Rademacher,
Fernotron, Amazon, Alexa and Echo are trademarks of their owners; this
project is not affiliated with or endorsed by them.

## License

Copyright 2026 Andreas. Licensed under the
[Apache License, Version 2.0](LICENSE); see [NOTICE](NOTICE) for
attributions.
