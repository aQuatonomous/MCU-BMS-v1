# BMS-MCU-V1 User Guide

How to **upload** firmware to the STM32, **read** data from it and the BQ76952, **write** settings to the BQ76952, and **run studies**.
Everything here was verified on the real board; where something is an assumption or unknown it is marked **(unverified)**.

> **Read the safety rules first (section 0). Then run `python3 tools/bms.py status` (section 3).**

---

## 0. Safety rules

1. **The FETs are OFF by default and the firmware never turns them on by itself.** Only `bms.py arm`, `fets-on` and the load-test / sweep tools turn them on, and only when you ask.
2. **Disconnect the load before `bms.py arm`.** `FET_ENABLE` makes the BQ switch CHG/PDSG/DSG **on immediately** (this board's behaviour). `arm` turns them off again about 4 s later.
3. **Right now only two BQ protections are active: SCD (short circuit, 10 A) and COV (cell overvoltage, 4250 mV), plus CUV (3289 mV).** There is **no** discharge-overcurrent (OCD), no charge-overcurrent (OCC) and **no temperature protection** (no thermistors attached). The tools' host-side guard (4.5 A, 3.3 V/cell, 60 °C) reacts in seconds, not microseconds. Do not rely on it as a protection.
4. **BQ settings live in the BQ's RAM.** They disappear whenever the BQ is reset or the BMS is power-cycled. The MCU re-applies them automatically within about 5 s (section 6), but a freshly power-cycled BQ has default (weaker) protections until then.
5. **Never put security keys or fuse / permanent-failure registers in the configuration file.** The generator blocks the security block outright and demands an explicit `FUSE-OK` acknowledgement for fuse / PF registers (section 6.1).
6. **Do not unplug the ST-Link or reset the MCU while the load is energised.** The firmware is designed so a reset does not enable anything, but a live test should not be interrupted.
7. The BQ76952's OTP (permanent memory) is **not** used: OTP writes are blocked on this part (Battery Status bit OTPB = 1) and nothing here programs it.

---

## 1. What is connected to what

```
 Mac ──USB── ST-Link V2 ──SWD (PA13/PA14)── STM32F205RET6 ──I2C (PB6 SCL / PB7 SDA, 7-bit addr 0x08)── BQ76952
                                                │  PC0 ── RST_SHUT (held LOW by the firmware from boot)
                                                │  USB-C (PA11/PA12) ── enumerates only; not used for any of the work below
                                      16 MHz crystal → PLL → 120 MHz core, 48 MHz USB
```

| Link | Used for |
|---|---|
| **ST-Link / SWD** | programming the MCU, reading its RAM (all measurements), writing small trigger values into its RAM |
| **I2C (MCU ↔ BQ)** | everything the MCU does to the BQ: reading registers/settings, writing settings, sending commands. The BQ answers **with a CRC byte after every data byte** (CRC mode is on). |
| **PC0 → RST_SHUT** | held low from boot. (Floating or high puts the BQ into a "shutdown pending" state.) |
| **USB-C** | not needed for anything in this guide (and currently not enumerating) |

Cells: the board has **4 real cells = BQ cell inputs 1, 2, 3 and 16** (VC3…VC15 are shorted together at the top of cell 3; VC16 = top of cell 4). BQ cells 4–15 read 0 / −1 mV by design.

### The three different kinds of "writing"

| What | Where it goes | Survives… |
|---|---|---|
| **Firmware** (the program) | MCU **flash**, sector 0, over SWD | power loss ✔ |
| **Small trigger values** (`g_log_req`, `g_autocfg_pause`, …) | MCU **RAM**, over SWD | nothing (cleared by MCU reset) |
| **BQ settings** (COV, CUV, SCD, …) | BQ **RAM**, over I2C, written *by the MCU firmware* | MCU reset ✔ — **BQ reset / power cycle ✘** (the MCU re-applies them) |

---

## 2. One-time setup

1. macOS with **STM32Cube for VS Code** installed. It provides all tools under `~/Library/Application Support/stm32cube/bundles/` (`programmer`, `gnu-tools-for-stm32`, `cmake`, `ninja`). The scripts find them automatically.
2. **Python 3** (3.11 here). No extra packages are needed. Always run scripts with `python3 -I …` as shown.
3. The ST-Link V2 plugged into the Mac and wired to the board's SWD header (SWDIO, SWCLK, GND, and target-voltage sense). The ST-Link serial is `37FF71064E573436D8421743`; to use a different probe: `export BMS_STLINK_SN=<serial>`.
4. Open a terminal in the project folder: `cd /Users/logancalder/Documents/Aquatonomous/BMS-MCU-V1`

Folder map:

| Folder | What is in it |
|---|---|
| `config/` | **the files you edit**: `bms_settings.csv` (BQ settings applied at start-up), `bms_options.csv` (how the auto-configuration behaves) |
| `tools/` | `bms.py` (the one command you use) and the scripts behind it |
| `Src/`, `Inc/` | firmware source (`Inc/bms_config_generated.h` is generated from `config/` — never edit it) |
| `data/` | everything the tools write (CSV results). See `data/README.md` |
| `docs/` | this guide, register maps, the running lab notes (`Claude-notes.md`), pin mapping |
| `STM32_I2C/` | TI's reference BQ769x2 code and header (register names/addresses) — reference only |
| `bq76952-main/` | a third-party BQ76952 library — **reference only, do not run it on this board (it rewrites protections)** |

---

## 3. Every day: check the whole system

```
python3 tools/bms.py status
```

Takes ~15 s, changes nothing. Healthy output looks like this (abridged):

```
== Links ==
  [OK] ST-Link                SN 37FF…1743 FW V2J37S7
  [OK] MCU over SWD           target 3.24 V, device 0x411
  [OK] MCU clock              g_clock_status=0 (0 = HSE+PLL ok), SYSCLK source PLL
  [OK] MCU core               running
  [OK] RST_SHUT (PC0)         driven low by the MCU
  [OK] I2C bus / BQ           lines SCL=1 SDA=1, BQ at 0x08: OK
  [OK] BQ identity            DEVICE_NUMBER 0x7695
  [..] BMS USB (1209:0001)    not seen (… not needed for SWD work)
== BQ state ==
  [OK] FETs                   … (all OFF)  FET_EN=clear
  [OK] Safety / PF flags      …
== Start-up configuration ==
  [OK] auto-config            OK_MATCH …
      0x9304 Vcell Mode  want 0x8007  live 0x8007   MATCH   … (one line per setting)
OVERALL: all checks passed
```

| If you see | Meaning / action |
|---|---|
| `[FAIL] ST-Link` | probe not seen: check the USB cable; another program (STM32CubeIDE debug session) may be holding it — close it |
| `[FAIL] MCU over SWD` | ST-Link works but the MCU doesn't answer: board power, SWD wires, or BOOT0 held high (release the BOOT0 button and press NRST) |
| `[FAIL] MCU clock` | crystal/PLL problem, or the firmware is not the expected build → `bms.py flash` |
| `[FAIL] I2C bus / BQ` with `SDA=0` | the BQ is holding SDA low (hung transfer). The firmware frees it automatically; if it persists, reset the MCU (NRST) and re-check; last resort: power-cycle the BMS |
| `BQ at 0x08: NO_BQ / NACK_ADDR` | BQ unpowered (BMS supply off) or asleep/unplugged |
| auto-config `DEFERRED - a FET is on` | normal while a load test runs; the MCU leaves the BQ alone while any FET is on |
| config lines show `DIFFERS` | the BQ was reset; the MCU will repair it within ~5 s — run `status` again |
| `FETs … SOME ARE ON` | something enabled the FETs: `python3 tools/bms.py fets-off` |

`status` also saves the register snapshot to `data/status/latest.csv`.

---

## 4. Uploading (flashing) the firmware to the STM32

Do this after **any** change in `Src/`, `Inc/` or `config/`.

```
python3 tools/bms.py flash          # build + program + verify + reset
```

What happens, step by step (so you can do it by hand if needed):

1. **Build** — `cmake --preset Debug` then `cmake --build --preset Debug` (tools from the STM32Cube bundles). Result: `build/Debug/BMS-MCU-V1.elf`. A successful build prints the flash/RAM usage (about 15 KB of 512 KB flash).
2. **Program** — `STM32_Programmer_CLI -c port=SWD sn=<serial> freq=1800 -d build/Debug/BMS-MCU-V1.elf -v -rst`
   - erases **only flash sector 0** (`0x08000000`), writes the image, **verifies** it by reading it back, then resets the MCU.
   - it never touches the option bytes, read-protection (level 0) or OTP.
3. **Check** — expect `Download verified successfully`. Then `python3 tools/bms.py status` (~15 s after the reset, once the auto-configuration has run).

Notes:
- Flashing resets **only the MCU**. The BQ keeps its settings and state.
- The tools look up RAM addresses in `build/Debug/BMS-MCU-V1.elf`. **That file must be the one that is flashed on the chip.** If you build without flashing, or flash an older ELF, the tools will read the wrong addresses. When in doubt run `bms.py flash` again.
- `bms.py build` builds without flashing. `bms.py flash --no-build` flashes the existing ELF.
- BOOT0 is low by default (boots the user flash). Holding the BOOT0 button during reset boots ST's built-in bootloader instead (recovery only).

---

## 5. Reading data

### 5.1 Quick look
`python3 tools/bms.py status` (section 3).

### 5.2 Full register snapshot → CSV
```
python3 tools/read_bms_csv.py --samples 3 --interval 2 --out data/my_snapshot.csv
```
Writes **two** files: `my_snapshot.csv` (live registers) and `my_snapshot_bq_config.csv` (BQ settings read-out). Read-only: nothing is written to the board.

`my_snapshot.csv` columns: `timestamp, sample, mcu_pass_count, i2c_idle_scl_sda, primary_probe, cmd_hex, register, i2c_status, raw_bytes_hex, crc_ok, value_unsigned, value_signed, value_hex, unit_note`.
Key registers: Control Status `0x00`, Safety Alert A/B/C `0x02/04/06`, Safety Status A/B/C `0x03/05/07`, Battery Status `0x12`, cell voltages `0x14…0x32` (mV), stack/PACK/LD `0x34/36/38`, CC2 current `0x3A`, Alarm Status/Raw/Enable `0x62/64/66`, internal temp `0x68`, FET Status `0x7F`, PF Alert/Status `0x0A…0x11`.

`my_snapshot_bq_config.csv` has one row per BQ setting read (112 entries: protections, thresholds, delays, balancing, PF/fuse, calibration, pin configs). Each row is **valid only if** `i2c_status=OK`, `checksum_ok_vs_request=True`, `crc_ok=True` and `echo_3E_3F` equals `request_hex`; `data_hex_lsb_first` is a 32-byte window starting at that address, so the **first byte(s) are the setting itself** (little-endian).

Decoding helpers: `python3 tools/check_limits.py <config csv>` writes `docs/Limits_check.md` (thresholds vs the ranges in `docs/Limits_defaults.md`). Bit meanings: `docs/Status_bit_mapping.md`, `docs/reg-mapping.md`, `docs/Balancing-Configs.md`.

**Units** (what the numbers mean):
| Value | Unit |
|---|---|
| cell voltages | mV |
| current (`0x3A`) | **mA, already gain/offset-corrected**, signed (negative = discharge); steps of ≈7.5 mA (1 ADC count). Sense resistor = 1 mΩ. |
| stack / PACK / LD | raw 16-bit unsigned counts — **scaling unverified** (the BQ's DA Configuration sets it; I have not converted these to volts) |
| internal temperature | assumed 0.1 K (**unverified**) |
| COV / CUV thresholds | counts × 50.6 mV |
| SCD threshold | index → mV table (0 = 10 mV … 5 = 100 mV …) → amps = mV / 1 mΩ |
| SCD delay | library says ≈15 µs per count (two library formulas differ by one count) |

### 5.3 Time series (the logger)
The MCU can record into its RAM at up to ~40 Hz and you dump it afterwards (the BQ stores nothing and is only *read*).
```
python3 tools/bms.py log start --period 50     # 50 ms between samples; stops by itself after 2048 samples (~100 s)
   … do your experiment …
python3 tools/bms.py log stop
python3 tools/bms.py log dump --out data/my_log.csv
```
- Each sample reads the fast channel (current, FET status) every time, and the slow channel (cells, stack, pack, LD, alarm, battery status, temperature) every 5th sample (`--slow-div`). Use `--slow-div 1` for everything every sample (then `--period` ≥ 30 ms).
- CSV columns include `t_ms, cell*_mV, current_mA, current_A, fet_status_hex, CHG/PCHG/DSG/PDSG, alarm_raw_hex, SSA…, battery_status_hex, temp_raw, read_error, slow_fresh`. `read_error=1` means a read failed (value 0xFFFF).
- While the logger runs, the background reads are paused and the auto-configuration leaves the BQ alone.

### 5.4 Reading a RAM variable by hand
```
arm-none-eabi-nm -S build/Debug/BMS-MCU-V1.elf | grep g_autocfg            # find the address (it changes between builds)
STM32_Programmer_CLI -c port=SWD sn=<serial> mode=HOTPLUG freq=1800 -r32 0x2000F778 0x40
```
`mode=HOTPLUG` means "attach without halting or resetting the MCU". (Use the full paths under `~/Library/Application Support/stm32cube/bundles/…` or `bms.py`.)

---

## 6. Writing settings to the BQ76952

### 6.1 The normal way: edit the settings file, then flash

1. Open **`config/bms_settings.csv`** (a spreadsheet or text editor). One row per BQ setting:

| Column | Meaning |
|---|---|
| `enabled` | `1` = applied at start-up, `0` = ignored |
| `name` | free text |
| `address` | the BQ data-memory address, e.g. `0x9278` (names/addresses: `STM32_I2C/BQ769x2Header.h`; sizes and bit maps: `BQ76952_tech_manual.pdf` section 13) |
| `bytes` | `1` or `2` (size of the register) |
| `value` | the **complete register value** (decimal or `0x…`). Bits you don't mention are written as 0, so for bit-mask registers give the full mask. |
| `ack` | leave empty, except `FUSE-OK` for fuse / permanent-failure registers |
| `meaning`, `notes` | documentation for the next person — please fill them in |

2. Optionally edit **`config/bms_options.csv`**: `autoconfig_enabled` (0/1), `first_run_delay_s`, `check_period_s` (how often the MCU re-checks; this is what repairs a power-cycled BQ), `keep_awake_period_s` (re-sends SLEEP_DISABLE; 0 = off).
3. Apply it:
```
python3 tools/bms.py config apply      # = gen_config.py (validate + write Inc/bms_config_generated.h) + build + flash + verify
python3 tools/bms.py status            # ~15 s later: every setting should say MATCH
```
(`bms.py config gen` only validates and regenerates; `bms.py config show` prints the two files.)

**Guard rails in the generator** — it stops with an error message if: the address is outside `0x9200–0x93FF`; it is in the **security block `0x9256–0x925E`** (never allowed); it is a fuse / permanent-failure register (`0x9231–0x9233`, `0x925F`, `0x92C0–0x92C3`, `0x92CB–0x92DF`) and `ack` isn't `FUSE-OK`; the value doesn't fit; an address is listed twice; more than 32 rows.

**The settings currently in `config/bms_settings.csv`** (power-up default → ours):

| Address | Setting | Value | What it does |
|---|---|---|---|
| `0x9304` | Vcell Mode | `0x8007` (default `0x0000`) | cells 1, 2, 3, 16 are real; the 12 shorted inputs are ignored (TRM 13.3.2.19: with `0x0000` every input counts as a cell) |
| `0x9278` | COV threshold | 84 → 4250.4 mV (default 86 = 4351.6 mV) | overvoltage trip |
| `0x9275` | CUV threshold | 65 → 3289.0 mV (default 50 = 2530 mV) | undervoltage trip |
| `0x9261` | Enabled Protections A | `0x8C` (default `0x88`) | SCD + COV + **CUV** on |
| `0x9286` | SCD threshold | index 0 → 10 mV → **10 A** | short-circuit trip |
| `0x9287` | SCD delay | **14** → ≈195–210 µs | **⚠ decision point:** a sweep (data/scd_delay_sweep.csv) showed raw 2…10 trip with the load and raw 11 passes 3/3. 14 adds margin. Change it here if you want a different trade-off (a longer delay lets a real short flow longer before the BQ opens DSG). |
| `0x9308` | FET Options | `0x1D` (default `0x0D`) | adds PDSG_EN: pre-discharge before DSG closes |
| `0x930E` | Predischarge timeout | 50 → 500 ms (default 5 = 50 ms) | DSG is forced on when this expires |
| `0x930F` | Predischarge stop delta | 0 (default 50) | voltage check disabled — LD is measured upstream of the ideal diodes and can't see the pre-charge, so it is **timing-only** |

**Deliberately not in the file:** enabling the FETs; temperature protections (no thermistors); cell balancing (`0x9335 = 0x00`, off); permanent-failure/fuse settings (`PF_FUSE` = 0, so a PF turns the FETs off but cannot blow the fuse); anything OTP.

### 6.2 How the auto-configuration behaves (so you can trust it)
Starting ~3 s after the MCU boots, and every 5 s after that, the firmware:
1. checks the BQ answers and that **no FET is on** (otherwise it does nothing — a live load must not be interrupted);
2. reads **every** listed setting (each read validated by status + checksum + CRC; if any read is invalid it writes nothing);
3. if all match → done; if some differ → writes **only the differences** in a single CONFIG_UPDATE session, then reads them back to verify.

It never sends FET_ENABLE / ALL_FETS_ON. State is visible in `bms.py status`:

| State | Meaning |
|---|---|
| `OK_MATCH` | all settings already correct |
| `APPLIED` | differences were written and verified |
| `DEFERRED - a FET is on` / `DEFERRED - logger running` / `PAUSED` | left alone on purpose |
| `NO_BQ` | the BQ isn't answering (unpowered / bus problem) |
| `CFGUPDATE_FAIL`, `WRITE_FAIL`, `VERIFY_FAIL`, `READ_FAIL` | something went wrong; it retries on the next check; see Troubleshooting |

**Pause it** whenever you change BQ settings by other means (studies do this automatically):
```
python3 tools/bms.py autoconfig pause      # until you resume or reset the MCU
python3 tools/bms.py autoconfig resume
python3 tools/bms.py autoconfig now        # force a check right now
```

### 6.3 One-off changes without reflashing (advanced; RAM only)
The firmware accepts a few guarded commands by writing a "magic number" into an MCU RAM variable (the variable clears itself immediately, so a command can never run twice). **Pause the auto-configuration first**, or it will put the old value back within 5 s.

| Variable (find address with `nm`) | Magic value | Effect |
|---|---|---|
| `g_bq_cmd_req` | `0xFE7E0100` / `…0101` / `…0102` | SLEEP_DISABLE / ALL_FETS_OFF / BQ software reset |
| `g_bq_fet_enable_req` | `0xFE7E0022` | FET_ENABLE (only if not yet enabled) — **FETs go on** |
| `g_bq_fets_on_req` | `0xFE7E0096` | ALL_FETS_ON (only if FET_EN is set) — **FETs go on** |
| `g_bq_cfg_req` | `0xFE7E0200 + n` | fixed one-byte writes: 0/1 FET Options `0x0D⇄0x1D`; 2/3 timeout `5⇄50`; 4/5 stop delta `50⇄0`; 6/7 SCD threshold `0⇄5`; 8/9 COV `86⇄84`; 10/11 CUV `50⇄65`; 12/13 Enabled A `0x88⇄0x8C` (each only if the current value is the expected one) |
| `g_bq_scdd_req` | `0xFE7E0300 + v` | SCD delay = `v` (1…31) |
| `g_bq_vcell_req` | `0xFE7E0400` / `…0401` | Vcell Mode `0x0000→0x8007` / back |
| `g_autocfg_pause` | `1` / `0` | pause / resume the auto-configuration |
| `g_autocfg_req` | `0xFE7E0500` | check/repair now |
| `g_log_req` | `0xFE7E1001` / `…1002` | logger start / stop |
| `g_rst_shut_req` | `0xFE7E0C01` / `…0C02` | PC0 → output low / 200 ms high pulse then low (clears a "shutdown pending" state) |

Result of a config write: `g_bq_cfg_res` = {done, reason, address, before, after, cfgupdate_seen}. `reason`: **0 ok**, 1 bad index, 2 couldn't read the current value, 3 current value not the expected one (nothing written), 4 BQ never entered CONFIG_UPDATE (nothing written), 5 I2C write failed, 6 read-back mismatch, 7 still in CONFIG_UPDATE.

Manual write example (the programmer CLI prints a harmless "Failed to download data" for RAM writes — **always read the result back**):
```
STM32_Programmer_CLI -c port=SWD sn=<serial> mode=HOTPLUG freq=1800 -w32 <address-of-g_bq_scdd_req> 0xFE7E030F   # SCD delay = 15
```
Easier: use `bms.py` (`fets-off`, `sleep-disable`, `bq-reset --yes`, `fets-on --yes`, `arm --yes`).

---

## 7. Running studies

### 7.1 Load test (current, voltage, FET sequence, with guards)
Preconditions: BQ healthy (`status` all OK), FETs off, **FET_EN set** (`bms.py arm --yes` with the load **disconnected**, once after every BQ reset).
```
python3 tools/bms.py loadtest --require-load --duration 20 --period 30
```
Sequence: SLEEP_DISABLE → logger on → ALL_FETS_ON (pre-discharge 500 ms, then DSG) → watch for `--duration` s → **ALL_FETS_OFF (always, even on error)** → CSV in `data/loadtest_<timestamp>.csv`.
Guards (stop immediately and turn everything off): |current| > 4.5 A, any cell < 3.3 V, temperature > 60 °C (units assumed), an SCD/safety flag, or the BQ opening DSG itself. Options: `python3 tools/run_load_test.py --help`.
Look at the CSV for: `ms` from PDSG to DSG, `SSA` (safety fault), `current_mA`, cell sag.

### 7.2 SCD-delay sweep (how long the short-circuit delay must be for your load)
```
python3 tools/bms.py sweep-scd                 # starts at raw 2, +1 per trial, stops after 3 passes in a row (or raw 31)
python3 tools/bms.py sweep-scd --start 11 --confirm 5
```
Needs the load attached and FET_EN set. It **pauses the auto-configuration**, sets the 10 A threshold, and at the end un-pauses it so the BQ returns to `config/bms_settings.csv`. Output: `data/scd_delay_sweep_<timestamp>.csv` (new file every run) and per-trial detail in `data/scd_sweep/<timestamp>/`. `TRIP` = SCD fired when DSG closed; `PASS` = DSG stayed on ≥ 3 s with no flag.

### 7.3 Adding your own reads or studies
- **A new read-only BQ setting to log:** append its address to `k_table[]` in `Src/bq_dm.c` (append only — fixed indices are used elsewhere), raise `BQ_DM_NENTRIES` in `Inc/bq_dm.h`, `bms.py flash`, then `read_bms_csv.py` shows it (names come from `STM32_I2C/BQ769x2Header.h`).
- **A new direct register in the logger:** `Src/logger.c` (`log_sample_t` in `Inc/logger.h`), then update `tools/dump_log_csv.py`.
- Read the TRM section first (`BQ76952_tech_manual.pdf`, section 13 "Data Memory Settings"). **Never write settings you haven't confirmed in the TRM.**

---

## 8. Command reference (`python3 tools/bms.py …`)

| Command | Does | Turns FETs on? |
|---|---|---|
| `status` | checks every link + BQ state + startup config | no |
| `build` / `flash [--no-build]` | compile / compile + program + verify + reset the MCU | no |
| `config show` / `gen` / `apply` | show / validate+regenerate / regenerate+flash the settings | no |
| `autoconfig pause|resume|now` | control the start-up auto-configuration | no |
| `log start [--period ms] [--slow-div n]` / `stop` / `dump [--out f]` | RAM logger | no |
| `sleep-disable` | keep the BQ out of SLEEP for a while | no |
| `fets-off` | ALL_FETS_OFF | no (turns them off) |
| `arm --yes` | FET_ENABLE then ALL_FETS_OFF (**load disconnected!**) | briefly, then off |
| `fets-on --yes` | ALL_FETS_ON | **yes** |
| `bq-reset --yes` | BQ software reset (same as a power cycle for its RAM) | no |
| `loadtest …` / `sweep-scd …` | the studies (7.1, 7.2) | **yes** (always ends with them off) |

---

## 9. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `No ST-LINK detected` / status stops at the first line | cable, or an IDE debug session holds the probe | replug, close STM32CubeIDE debug |
| `Failed to download data! If it's a Flash memory…` after a **RAM** write | harmless quirk of the programmer CLI | read the value back (the tools do) |
| tools print nonsense / wrong values after a code change | the ELF in `build/` isn't the one on the chip | `python3 tools/bms.py flash` |
| `i2c_status = BUS_BUSY`, lines `SDA=0` | the BQ is holding SDA low (hung transfer) | the firmware tries a 9-clock recovery by itself; else reset the MCU; last resort power-cycle the BMS |
| `NACK_ADDR` / `NO_BQ` | BQ unpowered, or in a deep state | check the BMS supply / pack connection; check `RST_SHUT` is low |
| config write reason **4** (no CONFIG_UPDATE) | the BQ was in SLEEP | normally fixed automatically (the writer sends SLEEP_DISABLE first); retry |
| BQ shows `SLEEP=1` | it falls back to sleep after ~20 s without `SLEEP_DISABLE` | keep `keep_awake_period_s` > 0 in `config/bms_options.csv` |
| Battery Status bit 13 (shutdown pending) set, FETs won't turn on | RST_SHUT was not low | firmware holds PC0 low from boot; if it persists: `g_rst_shut_req` pulse (6.3) |
| SCD trips (Alarm bit 14 / Safety A `0x80`) when DSG closes with the load | inrush above the SCD threshold longer than the delay | run the sweep (7.2); raise the delay (`0x9287`) or limit inrush on the load side |
| `loadtest`: `ABORT: FET_EN is not set` | BQ was reset (FET_EN clears) | `bms.py arm --yes` with the load **disconnected** |
| settings keep reverting | BQ reset / power cycle (RAM only) | nothing to do: the MCU re-applies within ~5 s. If not: `status` → look at the auto-config state |
| BMS USB device not seen on the Mac | USB-C plug not fully seated / no CC resistors seen by the host / not yet working | not needed for SWD work; not diagnosed yet |

---

## 10. Known open items (as of 2026-11-08)

- **SCD delay** (`config/bms_settings.csv`) is set to 14 on my own recommendation; the owner has not confirmed it (raw 11 is the minimum that passed).
- **Thermistors** not attached: temperature protections and the temperature limits of cell balancing are unconfigured/untested.
- **Cell balancing** is disabled (`0x9335 = 0x00`) and untested; the cells are currently produced by a resistor divider.
- **USB** enumeration on the Mac has not worked yet.
- The stack / PACK / LD **scaling** and the internal-temperature unit are unverified.
- The load used in tests only drew tens of mA; its high-current stage has not been exercised.
- Everything the BQ is told is RAM-only; OTP is not programmed.

*Detailed history of every experiment and decision: `docs/Claude-notes.md`.*
