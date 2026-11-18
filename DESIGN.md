# LoRa forest logger firmware: design

Implements the functional specification in `methodology.md`, Section 4.2, for two M5Stack Unit C6L (U202) boards, each with a Unit GPS v1.1 (U032-V11, AT6668) on the Grove port. Status: approved 18 Nov 2026; stage 1 implemented (see README.md), not yet tested on hardware.

## Intent

Two identical boards measure LoRa path loss between an EC tower (transmitter) and a pole-mounted mobile node (receiver). Every scheduled packet must leave a record at the receiver, received or not, so lost packets become censored observations. Correct timing, complete records and a fixed radio configuration matter more than features.

## Decided earlier (methodology, user)

- RadioLib SX126x driver, PlatformIO, pioarduino ESP32-C6 platform (same as `3a_loracore`), code in `firmware/`.
- 869.525 MHz, BW 125 kHz, CR 4/5, 8-symbol preamble, explicit header, CRC on, 14 dBm, 20-byte payload, private sync word.
- 15 s cycle on GNSS UTC: SF7 packet at +0 s, SF12 packet at +1 s.
- Receiver log fields as in methodology Section 6.2; tower keeps its own transmit log.
- Operator sets session, stop ID, pole height and mode from a phone over BLE (user choice, 18 Nov 2026).
- Logs stored in internal flash, downloaded over USB after each day.

## Data flow (user decisions, 18 Nov 2026)

- **Both boards log, different data.**
  - The **mobile node** logs one record per scheduled slot, received or not: the measurement data that are analysed (RSSI, SNR, signal RSSI, frequency error, position, stop, height).
  - The **tower node** logs what it actually transmitted: cycle, SF, transmit start time, GNSS state, temperature, and cycles it skipped (no GNSS time, outside the window, duty-cycle guard).
  - The analysis joins the two by cycle and SF. A slot counts as a lost (censored) packet only if the tower log shows it was transmitted; slots the tower skipped are dropped. Without the tower log, a tower outage would look like a propagation loss.
- **Buffering and download.** Records are buffered in RAM and written to LittleFS in internal flash. Download over USB with `scripts/lora_logs.py download` (USB serial).
  - USB mass-storage mode is not possible: the ESP32-C6 has only the fixed-function USB Serial/JTAG controller, which cannot act as anything but a serial port and JTAG (ESP-IDF documentation, "USB Serial/JTAG Controller Console").
  - The same port flashes new firmware at any time: esptool puts the chip into download mode automatically, even while the logger runs. Nothing in the firmware waits on or holds the USB port.
- **BLE** carries commands, live records and status, not the full log download.
- **User button** (expander P0, polled over I²C):
  - three quick presses write a marker record (operator event, e.g. pole moved or disturbance), confirmed by a white LED flash;
  - a long press of 5–10 s is the Wi-Fi action (below); a press longer than 10 s is ignored, so leaning on the button does nothing.
- **Display** (SSD1306 0.66", 64 × 48, SPI): shows the role in large letters ("TOWER" or "MOBILE") and the node ID, so the two boards cannot be confused, plus a few status lines.
  - Tower: GNSS state, transmit counter, Wi-Fi state.
  - Mobile: GNSS state, stop ID and pole height, last RSSI/SNR or "lost", Wi-Fi state.
  - The display shares SCK/MOSI with the radio; it is redrawn only between radio operations, with its chip-select released before any radio access.
- **Wi-Fi and cloud: stage 2, after the bench tests.** Station SSID, password and the cloud token are entered over USB only (e.g. `wifi add <ssid> <password>`, `wifi list`), not over BLE, which is unpaired and would carry the password in clear text. They are kept in NVS on each board, never in project files. Each board can hold the network of both stations. No Wi-Fi manager or captive portal (user decision, 18 Nov 2026): it would add an open access point at the station and an extra mode to fail in the field.
  - **Tower:** Wi-Fi on by default; the long press switches it off or on again. Every minute it sends its state to a cloud dashboard (e.g. ThingsBoard): transmitting or not, GNSS fix, temperature, packet counter.
  - **Mobile:** Wi-Fi off in normal work. Back at the tower after a transect, the long press starts Wi-Fi, joins a known station network, uploads the records not yet uploaded, and switches Wi-Fi off again. The display and LED show the result. The upload destination (cloud telemetry or file storage) is chosen in stage 2, after checking the free-plan limits of the service; USB download stays the archival path.
  - A LoRa back-channel from the mobile node is not planned.

## Builder's calls (to check)

1. **One code base, two build environments**: `tower` and `mobile`, plus `native` for host unit tests. No run-time role switch, so a board cannot start in the wrong role by accident.
2. **Cycle number** = floor(UTC Unix seconds / 15). Both nodes agree without a shared start time; the scheduled transmit time follows from the cycle and SF.
3. **Time base.** The arrival time (`micros()`) of each NMEA RMC sentence is paired with its UTC second. A constant NMEA latency (default 0 ms) can be set once from a two-node bench test. Both boards use the same GNSS model, so most of the latency cancels. Each RMC is stamped with the start of its NMEA burst (first '$' after a ≥ 200 ms pause; only a '$' can start a burst, because a late UART callback creates fake gaps inside sentences), which does not move with the number of sentences before RMC. After the last valid RMC the clock runs in holdover for up to 600 s (crystal drift < 12 ms); records carry a holdover flag. Without time for longer, the tower does not transmit and the mobile node writes no records.
4. **Receive windows**: SF7 from −0.4 s to +0.7 s, SF12 from +0.7 s to +14.6 s of each cycle. This tolerates about ±0.3 s clock offset between the nodes. After each window the receiver writes exactly one record for that slot, including a "not listened" record when the window was not monitored from at least 100 ms before the transmit time. Slot order uses a monotonic key (cycle·2 + slot), so a small backward clock step cannot reopen a slot. Flash writes, commands and dumps run only away from window boundaries and transmit times.
5. **Own NMEA parser** (RMC and GGA only, about 100 lines, unit-tested) instead of a library, so the time stamp handling is explicit.
6. **Interference monitor (deviation from methodology item 7).** The SX1262 drops packets with a foreign sync word in hardware, so they cannot be counted as packets. The firmware instead counts preamble detections not followed by a valid header, plus CRC errors, per slot.
7. **Supply voltage.** The Unit C6L has no documented battery or VBUS voltage divider. The `*_vbat_v` fields stay empty unless a measuring point is found on the board; board temperature comes from the ESP32-C6 internal sensor. Methodology Section 6.2 needs a note on this.
8. **Binary records on flash, CSV on the host.** Fixed-size 32-byte records on the 5 MB LittleFS partition give about 13 days of continuous slots (11 520 records per day), enough for the unattended fixed link between radio days. A CSV row of the same content would fill the flash in about 4 days. A host script converts the binary log to the CSV of methodology Section 6.2. Live output over USB serial and BLE is plain CSV text.
9. **Commands** over BLE (Nordic UART service, Arduino core's built-in BLE library: NimBLE-Arduino 2.2.3 does not build for the ESP32-C6 with this core) and USB serial, same parser: `session`, `stop`, `h`, `mode`, `status`, `files`, `dump`, `erase` and `format` (both with confirmation), and for the tower `window HH:MM-HH:MM` (UTC transmit window kept in NVS; default: transmit whenever time is valid).
10. **Duty-cycle guard.** The tower tracks transmit airtime over a rolling hour and stops transmitting above 360 s (10 %). The schedule itself uses 9.2 %.
11. **Status LED** (WS2812 on G2, Arduino-core `rgbLedWrite`, no extra library): mobile green = packet received, red = slot lost, yellow = no GNSS time; tower blue flash = transmit. Display driver: U8g2 (supports the 64 × 48 SSD1306 over SPI); buzzer unused.
12. **Board init** follows the Meshtastic variant (commit 968692d): PI4IOE5V6408 at 0x43 on I²C G10/G8, P7 = SX1262 reset (high = run), P6 = RF switch enable, P5 = LNA enable; DIO2 as RF switch; TCXO 3.0 V on DIO3.
13. **Context changes are deferred** to the end of the open receive window, which is flagged (`kFlagContextChanged`, CSV `ctx_changed`); otherwise a packet received at the old pole height would be written under the new one (audit 18 Nov 2026, `docs/audits/`).
14. **Fail-safe storage.** LittleFS is mounted without format-on-fail; formatting needs `format confirm`. A batch is written again after a failed write from its first missing record, so nothing is duplicated, and rotation waits until the old file's records are stored. Records lost to a full flash are counted.
15. **Watchdog.** The task watchdog (8 s, panic) watches the main loop, so a hang inside RadioLib (unbounded BUSY wait in `startTransmit`) resets the board; the TX timeout runs on `esp_timer`, independent of GNSS time. Each boot writes a `boot reset=<n>` marker.
16. **Tower timing.** A transmission is prepared 30 ms ahead and still sent up to 100 ms late (`core::txAction`); later it is logged as `late`. The receiver tolerates about 300 ms.
17. **Frame records** (type 4, one per received frame in our format): window cycle and SF, the packet's own cycle and SF, arrival time of RX_DONE (DIO1 edge, `esp_timer`) against the scheduled end of the packet, RSSI, SNR, tower node and sequence. They give the inter-node clock offset continuously and expose a UTC disagreement between the receivers (audit items 6 and 12).

## Packet (20 bytes, little-endian)

| Bytes | Field |
|---|---|
| 0 | magic 0x4C ('L') |
| 1 | format version |
| 2 | node ID |
| 3 | SF (7 or 12) |
| 4–7 | cycle number (uint32) |
| 8 | tower fix status (0 none, 1 time only, 2 2-D/3-D fix) |
| 9 | tower satellites |
| 10–11 | tower HDOP × 100 (uint16) |
| 12 | tower board temperature, °C (int8) |
| 13–14 | tower supply, mV (uint16; 0 = not measured) |
| 15 | firmware version |
| 16–17 | transmit sequence number (uint16) |
| 18–19 | CRC-16/CCITT over bytes 0–17 |

The application CRC catches packets that pass the LoRa CRC but are not ours (another network on the same sync word).

## Host tools

`scripts/lora_logs.py`: `download` (USB serial, writes a new folder under `data/raw/`, never overwrites) and `convert` (binary → CSV of methodology Section 6.2). Tested with pytest.

## Testing

- Host unit tests (`pio test -e native`): schedule and window arithmetic, packet encode/decode and CRC, NMEA parsing and time pairing, record encoding, command parsing.
- Build check: `pio run -e tower -e mobile`.
- On the boards (when connected): bring-up (expander, radio, GNSS), two-node link on the bench, then the bench tests in methodology Section 4.2 (timing, duty cycle over 1 h, RSSI linearity, freezer test).

## Not in scope now

Menus on the display, buzzer, over-the-air updates, LoRaWAN. Wi-Fi, cloud reporting and the mobile node's Wi-Fi upload come in stage 2 (see Data flow); stage 1 already implements the button gestures and the display.
