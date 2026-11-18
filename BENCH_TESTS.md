# Bench tests with the boards

What the host tests cannot check. Run before the first field campaign (W1, January 2027), in this order. Numbers in brackets refer to `docs/audits/2026-11-18-firmware-audit.md`; methodology sections to `methodology.md`. Record every result (date, firmware version, both board IDs, pass/fail, numbers) in `data/meta/bench_log.csv` or a session note; raw logs go to `data/raw/` as usual.

Needs: both Unit C6L boards with Unit GPS v1.1, USB cables, a laptop with PlatformIO, an SMA cable and fixed attenuators (60–100 dB) with a 50 Ω load, a USB power meter, a phone with a BLE UART terminal, a freezer.

## 1. Bring-up (each board)

1. Flash `pio run -e tower -t upload` / `-e mobile -t upload`; `status` answers with role, node ID and firmware version.
2. No `FATAL` line; display shows the role and node ID.
3. GNSS: `status` shows `time ok` within a few minutes outdoors.
4. Radio: tower blue LED flashes at +0 s and +1 s of each 15 s cycle; mobile shows results.
5. **Regulator [9].** With the antenna on a 50 Ω load or attenuator, measure TX current (USB power meter) and, if a power meter or SDR is available, conducted power, once with DC-DC (current firmware) and once with LDO (`sx.begin(..., true)` in `radio.cpp`). Keep the mode with correct output power; fix it in `config.h`.
6. **Reset [9].** Restart ten times; the radio must start every time. If not, add an explicit P7 low pulse in `board::begin()`.

## 2. Timing between the nodes

1. **Two-node offset [6, 18].** Both boards outdoors with GNSS, 1 m apart, 1 h. Convert the mobile log; in `_frames.csv`, `arrival_offset_us` must be stable (spread < 5 ms) and `cycle_diff = 0`. The median offset is the radio and NMEA latency; it should be well inside ±300 ms.
2. **NMEA burst structure [18].** Log the raw NMEA of one board for 2 min (`pio device monitor` on the GPS UART via a USB-UART adapter, or a temporary debug build). Confirm one burst per second, no internal pause close to 200 ms, and note which sentences come first. If a pause ≥ 200 ms occurs inside a burst, raise `kBurstGapUs`.
3. **Latency calibration.** If the median offset from 1 is not near the expected radio processing time, set `latency <ms>` on both boards as described in the methodology.
4. **GNSS loss and reacquisition [6, 10, 23].** Cover the mobile GNSS antenna (metal box) for 2 min, then 12 min. Expect: `holdover = 1` from the first void RMC; records continue during the 10-min holdover; after 10 min no records; after uncovering, no duplicate `(cycle, sf)` rows and `arrival_offset_us` back at its value.
5. **Cold start leap seconds [12].** Power both boards from cold (antennas disconnected for 30 min beforehand) and start together; for the first 15 min watch `cycle_diff` in `_frames.csv`. Any non-zero value means one receiver has not applied the leap seconds yet: wait until both agree before measuring, and add this to the field protocol.

## 3. Robustness

1. **Watchdog [4].** Temporary debug build with a `for (;;) {}` behind a test command, or hold the SX1262 BUSY line high: the board must reset within about 8 s, start a new log file and write `boot reset=4` or `=6` (panic or task watchdog; note which one appears). Remove the debug code afterwards.
2. **Power cut [5, 13].** Pull the power during logging ten times at random moments. After each restart: the log partition mounts (no `FLASH ERR`), every file decodes, and at most the last 16 records (≤ 60 s) are missing. Note the loss per cut.
3. **Flash full [5].** Temporary build with a 300 kB partition limit or fill the flash with dummy files: `LOG FULL` appears, `status` counts dropped records, and `erase` of old files resumes logging.
4. **Indoor download [17].** After a transect, take the mobile board indoors until `time NONE`, run `scripts/lora_logs.py download`; the open file is closed and downloaded, `erase all confirm` frees it, and after going outdoors a new file starts.
5. **Download from a running tower [11].** Run `download` while the tower transmits; count `late` slots in its `_tx.csv`. If more than a few, download only outside the transmit window.
6. **Context change [3].** During an SF12 window send `h 3.0`: the answer is `OK from next slot`, then `CTX ...` at the window end; the window in progress has `ctx_changed = 1` and the old height.

## 4. Radio performance

1. **Self-interference [21].** Conducted link through attenuators near the SF12 limit (as far as the attenuators allow), 30 min each with the display redraw and flash writes as now, and with both disabled (temporary build). Compare packet error rate and RSSI. A difference means service work must be restricted to +2.7 … +14.4 s of the cycle.
2. **RSSI linearity and unit offset [1].** Conducted check of methodology Section 5.4 (−70 to −110 dBm), both SFs, both boards in both roles. Compare `signal_rssi_dbm` with RSSI + SNR; note where SignalRssiPkt reaches −127.5 dBm in the field data later.
3. **Duty cycle.** One hour of tower operation: `status` duty ≤ 9.2 %; RF-on time from `_tx.csv` (sent SF7 × 56.6 ms + sent SF12 × 1318.9 ms) ≤ 360 s.

## 5. Temperature

1. **Freezer test [8].** Both boards in a freezer (−18 °C) for 2 h, logging over a conducted link or short range. Expect: no resets, `rx_temp_c` and `tx_temp_c` plausible (chip temperature, a few °C above the air), RSSI drift with temperature recorded for the uncertainty budget.
