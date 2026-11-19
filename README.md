# LoRa forest logger firmware

Stage-1 firmware for two M5Stack Unit C6L (U202) boards, each with a Unit GPS v1.1 on the Grove port. One board is the **tower** node (transmitter), the other the **mobile** node (receiver and logger). Design: [DESIGN.md](DESIGN.md); method: `../methodology.md`, Sections 4.2–4.3 and 6.2.

## Build and flash

Tools: PlatformIO (`~/.platformio/penv/bin/pio`). Run from this folder.

```bash
~/.platformio/penv/bin/pio test -e native
```

```bash
~/.platformio/penv/bin/pio run -e tower -t upload --upload-port /dev/cu.usbmodemXXXX
```

```bash
~/.platformio/penv/bin/pio run -e mobile -t upload --upload-port /dev/cu.usbmodemXXXX
```

- The role is fixed at build time; the display shows **TOWER** or **MOBILE** with the node ID.
- New firmware can be flashed at any time over the same USB-C cable, even while logging: the ESP32-C6 USB Serial/JTAG port switches to download mode on its own. The log files in flash survive a firmware update (separate LittleFS partition).
- Give each board a label (tower / mobile) as well; the display only shows what was flashed.

## Commands (USB serial at 115200 or BLE "LORA-TOWER-1" / "LORA-MOBILE-2")

For BLE use any Nordic-UART terminal on the phone, e.g. *Serial Bluetooth Terminal* (Android) or *nRF Connect*. Commands end with Enter.

| Command | Node | Effect |
|---|---|---|
| `help` | both | list commands |
| `status` | both | role, GNSS time and fix, bytes received from the GPS since boot (`nmea N B`; 0 means a silent GPS), log file, free flash, counters |
| `gps` | both | last 32 raw NMEA lines from the receiver; `$GNRMC,,V,...` with an empty time field means the receiver tracks no satellite at all |
| `gps reset` | both | CASIC factory reset of the GPS receiver (undoes configuration left by other firmware, e.g. Meshtastic), then 115200 baud restored and saved; cold start follows |
| `session W1_ME_20270112` | mobile | session ID (≤ 14 chars: letters, digits, `-`, `_`) |
| `stop ME-T1-100` | mobile | stop ID written into every following record |
| `h 1.3` | mobile | pole height in m |
| `mode static` | mobile | `static`, `sweep`, `reference` or `drift` |
| `marker <text>` | both | operator note in the log (≤ 22 chars) |
| `window 07:00-17:00` / `window off` | tower | daily UTC transmit window (default off = transmit whenever GNSS time is valid) |
| `files` | both | list log files with sizes |
| `dump L0003.bin` | both | send a file (used by the download script); dumping the file being written first closes it (also indoors without GNSS time), so a downloaded file never changes; the next file opens once time is valid |
| `erase L0003.bin confirm` / `erase all confirm` | both | delete files (never the file being written) |
| `format confirm` | both | erase the whole log partition (provisioning, or after `FLASH ERR`); the firmware never formats on its own |
| `latency <ms>` | both | NMEA latency correction from the two-node bench test |
| `id <n>` | both | node ID (1–254), effective after restart |

Each change of session, stop, height or mode is logged as a context record. A change sent while a receive window is open (the usual case) takes effect when that window closes: the board answers `OK from next slot: ...`, then `CTX ...` when it applies. The window during which the change arrived keeps the old context and is flagged `ctx_changed = 1`, because its packet may have been received before or after the pole moved.

**Flash and resets.** If the log partition cannot be mounted, the board shows `FLASH ERR` and waits; it never formats by itself, so undownloaded logs are not lost. `format confirm` over USB erases the partition and restarts. A hung main loop resets the board after 8 s (task watchdog); every boot writes a marker `boot reset=<n>` (ESP-IDF `esp_reset_reason()`: 1 power-on, 4 panic, 5 interrupt watchdog, 6 task watchdog, 9 brown-out) at the start of the new log file. A new log file never reuses an existing name.

## LED and display

| Signal | Meaning |
|---|---|
| yellow (steady) | no GNSS time (never had a fix, or the last valid fix is > 10 min old): the tower does not transmit, the mobile node does not log slots |
| blue flash | tower: packet on air |
| green flash | mobile: packet received in this slot |
| red flash | mobile: slot ended without our packet (logged as lost) |
| white flash | marker written |
| red blinking at start | radio or flash failed to initialise (details on USB serial) |

Display lines: tower `GPS ok 11s` / `TX 1234` / `WIN off`; mobile stop ID / `h1.3 GPS11` (or `h1.3 HOLD` = no current fix, clock in holdover) / last result (`12:-112.5` or `7:LOST`). `LOG FULL` means flash is full and logging has stopped.

**GNSS holdover.** After the last valid fix the clock keeps the schedule for up to 10 min (crystal drift < 12 ms, far inside the ±300 ms receive windows), so a short loss of fix under the canopy does not stop the measurement. Records made in holdover carry `holdover = 1`, from the first RMC that reports the fix lost. The position in such records is the last known one; `rx_fix = 0` marks it as stale.

**Button** (side button): three quick presses = marker "button". Holding it 5–10 s shows `LET GO=WIFI`; releasing then triggers the Wi-Fi action (stage 2; now it only shows `WIFI:stage2`). Holding longer than 10 s does nothing.

## Field use (mobile node)

1. Power both nodes; wait for the yellow LED to go off (GNSS time valid, usually < 1 min outdoors).
2. On the phone: `session ...`, then at each stop `stop ...`, `h ...`, `mode static`. The live line per slot is `stop,h,cycle,sf,received,rssi,snr,signal_rssi`.
3. Change the stop before moving; slots logged while walking keep the old stop ID unless `mode sweep` is set.

## Download and convert

```bash
~/.platformio/penv/bin/python ../scripts/lora_logs.py download --port /dev/cu.usbmodemXXXX
```

```bash
python3 ../scripts/lora_logs.py convert ../data/raw/20270112_mobile2/L0003.bin
```

- `download` copies every file to `data/raw/<YYYYMMDD>_<role><id>/`, checks the CRC-32 of each, and never overwrites: a file already present with the same content is skipped; a differing one (should not happen) is kept as `..._v2.bin`.
- `convert` writes `<prefix>_<file>_rx.csv` (methodology Section 6.2 columns plus `rx_fix, time_valid, holdover, listened, foreign, ctx_changed, preamble_only, tx_seq, tx_temp_c, node_id`), `..._frames.csv` (one row per received frame in our format: `cycle_diff`, `arrival_offset_us`), `..._tx.csv` (tower) and `..._markers.csv` to `output/logs/`. The prefix is the download folder name (`20270112_mobile2`), otherwise `<role><id>`. Existing CSVs are never replaced unless `--overwrite` is given.
- `arrival_offset_us` (RX_DONE minus the scheduled end of the packet) should stay constant within a few ms; a drift or jump means a clock problem at one node. `cycle_diff ≠ 0` means the two GNSS receivers disagree on UTC.
- Every scheduled slot has one row. `listened = 0` means the window was not monitored from before the transmit time (e.g. the logger had just started): such rows are not lost packets.
- Analysis joins mobile `_rx` rows with tower `_tx` rows on `cycle` and `sf`; a slot counts as lost only if `listened = 1` at the mobile node and the tower log says `sent`. Tower statuses: `sent`, `outside_window`, `duty_guard`, `radio_error`, `late` (loop more than 100 ms late; up to 100 ms late the packet is still sent and `tx_offset_us` shows the delay). A missing mobile row means "not observed", never "lost".

## Log format

32-byte little-endian records (types: header, context, RX slot, RX frame, TX, marker), defined in `lib/core/src/record.h` and decoded by `src/lora_forest/logfile.py`. A test vector shared by the C++ and Python tests keeps the two in step. Flash (13.5 MB LittleFS partition of the 16 MB chip) holds about 35 days of continuous slots per node.

## Radio and sensor details

- RX boosted gain is on (set explicitly; it fixes the sensitivity and so the censoring threshold).
- `tx_offset_us` is measured right after the SetTx command; the TCXO start and PA ramp-up inside the SX1262 (a few ms) come on top of it.
- Board temperature: ESP32-C6 internal sensor, configured for −10…80 °C (the core's default range starts at 10 °C).

## Not yet verified on hardware

All code is built and unit-tested on the computer only. Before the pilot: board bring-up (expander, radio, GNSS, display), a two-node bench link, NMEA latency (`latency`), then the bench tests of methodology Section 4.2.
