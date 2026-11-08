# Claude notes

## VBUS sensing disabled for USB (`Src/usb_fs.c`)
The OTG_FS core normally uses PA9 as its VBUS-sense input. On this board PA9 is wired
as USART1_TX (see Connection-Mapping.md), so there is no VBUS signal for the core to
see. `usb_fs_core_init()` therefore sets `GCCFG.NOVBUSSENS` and clears `VBUSASEN` and
`VBUSBSEN`. Without that, the core would wait for a VBUS session that never arrives and
the device would not enumerate. PA10 (OTG_FS_ID) is unused in device mode, so it is not
configured.

The USB core is initialised in device mode but left **soft-disconnected**
(`DCTL.SDIS = 1`), so the host sees nothing until a USB stack is added.

## Clock configuration (`Src/clock.c`)
HSE 16 MHz -> PLL: M=16, N=240, P=2, Q=5 -> VCO 240 MHz, SYSCLK 120 MHz, USB 48 MHz.
AHB /1 (120), APB1 /4 (30), APB2 /2 (60). Flash: 3 wait states, prefetch + I/D cache on.
If HSE or the PLL fails to come up, the MCU stays on HSI and `g_clock_status` holds the
reason (0 = OK, 1 = HSE, 2 = PLL, 3 = switch failed); USB is not touched.

## File layout
- ST/ARM CMSIS headers supplied by the project owner live in `Inc/`
  (`stm32f205xx.h`, `core_cm3.h`, `cmsis_*.h`, `mpu_armv7.h`, `system_stm32f2xx.h`).
- `Src/system_stm32f2xx.c` provides `SystemInit()` (required by the startup file) and
  `SystemCoreClockUpdate()`.
- New sources are listed in the user section of `CMakeLists.txt`; `cmake/files.cmake`
  is managed and was not edited.

## Hardware checks done so far (nothing written to flash)
- Read-only SWD reads: device ID 0x411 (F2xx), 512 KB flash, RDP level 0, no write
  protection, flash blank, RCC at reset defaults (HSI 16 MHz), VDD 3.25 V.
- Temporary RAM-only test (restored afterwards): HSE reached HSERDY and the PLL reached
  PLLRDY with the agreed factors. That shows the crystal oscillates and the PLL locks;
  it does not measure the crystal frequency.
- Quirk: `STM32_Programmer_CLI -w32` to peripheral registers prints "Failed to download
  data" on some writes even though the write took effect. Always read the register back.

## Boot pins
BOOT0 has a resistor to ground plus a push button to VDD, so it is low by default
(boots user flash). Holding the button during reset boots the ST system bootloader.

## BQ76952 first contact over I2C (read-only) - 2026-11-07
- `Src/i2c_bb.c` is a bit-banged I2C master on PB6 (SCL) / PB7 (SDA): open-drain, no internal
  pull-ups. Chosen over the F2 hardware I2C block for first bring-up (visible line levels,
  no peripheral quirks). Hardware I2C1 (AF4) can replace it later.
- `Src/bms_probe.c` reads only BQ direct-command registers below 0x3E (pointer write, repeated
  START, read) every 0.5 s. It never writes data/subcommands (0x3E/0x40/0x60) and never
  touches PC0-PC5 (RST_SHUT, DFETOFF, CFETOFF, DDSG, DCHG, ALERT).
- Result: SCL/SDA idle high (pull-ups work); the BQ ACKs at 7-bit 0x08; all reads OK.
- **The BQ is replying with CRC** (each data byte is followed by a CRC-8, poly 0x07, over
  {0x10, cmd, 0x11, data} for the first byte and over the byte alone for later bytes).
  Any future driver must append/check CRCs, including on writes.
- Results live in `g_bms_dbg` (raw bytes + status per register), readable over SWD.

## DSG investigation, step 1 (read-only direct commands) - 2026-11-07
- Probe extended with SafetyAlert A/B/C, AlarmStatus/RawStatus/Enable and FETStatus (0x7F);
  `g_bms_dbg` is now self-describing (cmd, len, status, raw per record) and
  `tools/read_bms_csv.py` sizes itself from the ELF.
- FETStatus read 0x30: bits 0-3 (CHG, PCHG, DSG, PDSG) all 0 = the BQ is not turning on DSG
  or CHG. Bits 4-5 are set; their meaning is not defined in any file in this repo.
- Safety alert/status A/B/C all 0.
- NOT done: reading data memory (FET Options 0x9308, DSG FET Protections 0x9269-0x926B,
  DFETOFF/CFETOFF pin config 0x92FB/0x92FA, Manufacturing Status subcommand 0x0057). These need
  writes to 0x3E/0x3F, and possibly an unseal; waiting for the owner's go-ahead.
- `bq76952-main/` is write-heavy (it reprograms protections and FET options). Used as a
  reference for addresses and protocol only; it must not be run against this board as-is.

## DSG investigation, step 2 - BQ configuration read-out (approved by owner) - 2026-11-07
- `Src/bq_dm.c` reads BQ subcommands/data memory using the documented 0x3E/0x3F request +
  0x40/0x60/0x61 reply mechanism. A FIXED table of 28 addresses is the only thing it can
  request (no function takes an arbitrary address); no CONFIG_UPDATE, unseal, FET control or
  data-memory write is issued. Replies are validated (checksum + per-byte CRC).
- Sanity check of the mechanism: DEVICE_NUMBER (0x0001) = 0x7695, as the library states.
- Each data-memory reply is a 32-byte window starting at the requested address; the first
  byte(s) are the parameter itself. 3 passes gave identical results (28/28 valid).
- A first attempt (8-byte capture, older firmware) returned inconsistent data (readback
  0x3E/0x3F = request + 0x60, no valid checksum). Its CSV was deleted. The cause was not
  determined; the later full-buffer reads are self-consistent and repeatable.
- Result CSV: data/bq_config_20261107_132808.csv (written by tools/read_bms_csv.py).
- Manufacturing Status (0x0057) = 0x0040; FET Status = 0x30 (CHG/PCHG/DSG/PDSG all off).
  Per the comments in bq76952-main/bq76952.cpp (and TI's example calling FET_ENABLE 0x0022
  during init), Manufacturing Status bit 4 = FET_EN, which here is 0. NOT yet acted on:
  the FET_ENABLE subcommand toggles that state and would change the BQ.

## DSG investigation, step 3 - FET_ENABLE sent once (approved by owner) - 2026-11-07
- Sent subcommand 0x0022 once via the one-shot mailbox (`g_bq_fet_enable_req`); the firmware
  re-verified Manufacturing Status first (bit 4 was 0) and only then wrote.
- Manufacturing Status 0x0040 -> 0x0050 (FET_EN set). **Data-memory entry "Mfg Status Init"
  (0x9343) also changed 0x0040 -> 0x0050** - the toggle altered a data-memory value, not just the
  live status. Whether it survives a power cycle was not tested (Battery Status OTPB=1: OTP
  writes blocked; OTPW=0: none pending).
- FET Status stayed 0x30 and Alarm Raw 0x06E3 (XDSG/XCHG set): enabling FET_EN alone did NOT turn
  DSG/CHG on. Safety registers still 0.
- Recordings: data/before_fet_enable*.csv, data/after_fet_enable*.csv (3 samples).
- Side note: the mailbox is a RAM word; nothing re-triggers it on reset.

## DSG investigation, step 4 - ALL_FETS_ON sent once (approved by owner) - 2026-11-07
- Sent subcommand 0x0096 once via the one-shot mailbox (`g_bq_fets_on_req`), after verifying
  Manufacturing Status = 0x0050 (FET_EN set). Not a toggle; ALL_FETS_OFF (0x0095) would reverse.
- NO visible effect: FET Status stayed 0x30 (CHG/PCHG/DSG/PDSG all off, 50 ms and ~15 s later),
  Alarm Raw stayed 0x06E3 (XCHG, XDSG set), Battery Status 0x2184, safety registers all 0,
  no config entry changed. Owner's own measurement: DSG still off.
- Cause NOT determined. Config facts so far: DFETOFF/CFETOFF/ALERT/DCHG/DDSG/HDQ pin configs
  all 0x00; FET Options 0x0D; no protection active. Open items: Battery Status bit 13
  (shutdown pending) still set; DDSG measured 0.75 V while FET Status says DDSG pin asserted.
- Current BQ state differs from power-up: FET_EN set (live and in the Mfg Status Init data-memory
  value). Whether a power cycle restores it was not tested.
- Recordings: data/before_fets_on*.csv, data/after_fets_on*.csv.

## DSG investigation, step 5 - RST_SHUT (PC0) sequence (owner-directed) - 2026-11-07
- Fact checked first: the firmware had never driven PC0 (GPIOC clock was off), so the 1.1 V
  seen on RST_SHUT did not come from this firmware.
- New one-shot control `Src/rst_shut.c` (mailbox `g_rst_shut_req`; touches PC0 only):
  stage A = PC0 push-pull output, driven LOW (latch low before enabling output);
  stage B = PC0 HIGH for 200 ms, then LOW (pulse clamped 20..900 ms).
- Stage A alone: no change (Battery Status 0x2184, FET Status 0x30).
- Stage B (200 ms pulse; MCU read PC0 high during, low after): Battery Status 0x0184 (bit 13 SDM
  now 0), FET Status 0x05 (CHG and DSG bits set; DCHG/DDSG pin bits 0), Alarm Raw 0x0683
  (XCHG/XDSG clear). BQ was NOT reset (Manufacturing Status still 0x0050, I2C kept answering).
- ALL_FETS_ON (0x0096) was NOT sent: FET_EN already set and CHG/DSG already on after the pulse.
- Earlier, one sample (~13 s after an MCU reflash, nothing sent to the BQ/PC0) showed this same
  "good" state (0x0184 / 0x05) and then returned to 0x2184 / 0x30. Cause unknown.
- PC0 is now held LOW by the MCU until the next reset. Recordings: data/after_pc0_low*.csv,
  data/after_pulse*.csv, data/stability_check*.csv.
- ~30 s after the pulse the BQ state moved again (stability_check): Battery Status 0x8184
  (bit 15 SLEEP now set; SDM still 0), FET Status 0x14 (DSG bit set, CHG bit now 0), Alarm Raw
  0x06C2 (XCHG set, XDSG clear), LD pin raw 1486 -> 2626. SLEEP_EN (bit 2) is set, so the BQ
  entering SLEEP is allowed; why CHG dropped and what SLEEP does to the FETs is not confirmed.
  TI's example sends SLEEP_DISABLE after init; NOT sent here (needs owner approval).

## Load-test preparation - 2026-11-07
- **RST_SHUT (PC0) is now held LOW from boot** (`rst_shut_init_low()` first thing in main; owner-approved).
  Verified after reflash: GPIOC MODER=output, ODR=0.
- **Logger** (`Src/logger.c`, owner-approved): 20 Hz default, 2048-sample RAM buffer (~100 s), start/stop over SWD
  (`g_log_req` 0xFE7E1001 / 0xFE7E1002), dump with `tools/dump_log_csv.py`. Read-only on the BQ. Background
  passes are paused while logging. Dry run: 125 samples in 6.2 s, steady 50 ms, 0 read errors. A first dry run
  exposed a bug in my read helper (it wrongly rejected direct commands >= 0x3E, e.g. 0x64/0x68/0x7F); fixed.
- **One-shot commands** (`g_bq_cmd_req` = 0xFE7E0100 | index): 0 = SLEEP_DISABLE (0x009A), 1 = ALL_FETS_OFF (0x0095).
  Built and NOT sent.
- **Config read-out extended to 78 read-only entries** (thresholds, delays, recovery, shutdown, calibration, masks).
  All 78 valid. Decoded vs docs/Limits_defaults.md in docs/Limits_check.md (tools/check_limits.py).
- Effective protections on the chip: Enabled Protections A/B/C = 0x88/0x00/0x00 => only SCD (0x9286 index 0 =
  10 mV = 10 A @ 1 mOhm) and COV (4.352 V) are active. OCD1 8 A / OCD2 6 A / CUV 2.53 V / temperature
  thresholds are SET but their checks are NOT enabled. (Bit names from the library; layout assumed shared.)
- Doc vs library disagreement: CUV upper limit (doc raw 80 = 4.048 V; library accepts raw 90 = 4.554 V).
- OBSERVED: after an MCU reflash/reset the BQ showed Battery 0x0184 / FET 0x05 (CHG and DSG ON) with no
  command from me (BQ RAM still holds FET_EN=1 from earlier). Always verify FET Status after any MCU reset.

## Load test run 1 - 2026-11-07 (DSG never turned on)
- Sequence executed (owner "go", 50 ms logging): SLEEP_DISABLE -> logger start -> FET_ENABLE (Mfg 0x40->0x50)
  -> ALL_FETS_ON (sent once; FET Status 50 ms later 0x70). Log: data/load_run1.csv (2048 samples, 102.35 s, 0 read errors).
- **DSG was off for the entire run** (no DSG=1 sample; XDSG set; max |cc2| = 22 counts, i.e. essentially no current).
- At ~3.6 s (when FET_ENABLE took effect) Alarm Raw gained bit 14 (SSA) and FET Status gained bit 6; and
  **Safety Status A = 0x80 (bit 7 = SCD, short-circuit in discharge, per library bit comments) is still set** live
  after the run (AlarmStatus latched 0x4000, Battery Status SS bit toggling 0x0988/0x0188). Safety Alert A/B/C = 0,
  Safety Status B/C = 0. DSG FET Protections A (0xE4) includes SCD, so the BQ holds DSG off. NOT overridden.
- Not determined: why SCD is flagged with ~no measured current and the load switched off. Not done: any attempt
  to clear/override the SCD flag (no verified method in the references; overriding would defeat a protection).
- Tool fix: dump_log_csv.py now reads in <=32 KB chunks (CLI limit), needed for full-length logs.

## After run 1 - units, lean logger, software reset - 2026-11-07
- **Current units:** 0x3A is already gain-applied and in mA (DA Config 0x05: USER_AMPS bits = 01 = mA per library;
  logged values step by ~7.5 = CC Gain). My earlier "current_A_assumed" column double-converted and was wrong.
  dump_log_csv.py now emits current_mA / current_A (= mA x 1e-3); data/load_run1.csv was corrected in place.
  Run-1 current stayed within +/-22 mA (about 3 ADC counts) - noise level, no load current.
- **Lean logger** (owner request): all samples live in MCU RAM only; the BQ stores nothing and is never written by it.
  Fast channel (CC2 + FET status) every 50 ms; slow channel (cells, stack, pack, LD, alarm, battery, temp) every 5th
  sample, held in between (g_log_slow_div). ~80 reads/s instead of 240/s. Dry run: 165 samples, 50 ms steady, 0 errors.
- **BQ software RESET (0x0012)** added to the one-shot table (index 2) and sent once (owner asked to refresh the SCD
  flag without a physical restart). Result: BQ re-initialised exactly like a power cycle - POR set, Safety Status A
  0x80 -> 0x00, SS bit clear, latched alarm 0, Mfg Status 0x0050 -> 0x0040 (FET_EN cleared), SLEEP back, DSG off.
  No documented command that clears only the SCD flag was found in the references.
- Observed: SLEEP_DISABLE did not persist (BQ showed SLEEP again before the reset). Re-send it after any BQ reset.

## Clean retry with the load DISCONNECTED - 2026-11-07
- Sequence: SLEEP_DISABLE -> lean logger -> FET_ENABLE -> ALL_FETS_ON. Log: data/retry_noload.csv (428 samples, 0 errors).
- **With no load, FET_ENABLE alone turned CHG and DSG ON (FET Status 0x05) and NO SCD latched** (Safety Status A stayed 0,
  Alarm SSA never set). ALL_FETS_ON changed nothing further. DSG stayed on for the rest of the log.
- Run 1 (load connected) latched SCD (Safety Status A 0x80) at the moment FET_ENABLE took effect and DSG never came on.
  => the SCD trip is associated with the connected load at DSG turn-on, not with the BQ or sense path alone.
  Hypothesis (NOT verified): inrush into the load's input capacitance exceeding the SCD threshold (10 A) for longer
  than the SCD delay; the averaged current register (~250 ms scale) would not show a short spike.
- Afterwards the outputs were returned to the safe state with ALL_FETS_OFF (0x0095) because a load was about to be reconnected.

## Pre-discharge (PDSG) enable - prepared, NOT yet sent - 2026-11-07
- Owner confirmed the board has pre-discharge hardware. Mechanism: FET Options (0x9308) bit 4. The library's comment on
  setFET_Options says 0x1D enables PDSG_EN (+ FET_CTRL_EN, HOST_FET_EN, SFET) and that the device default is 0x0D;
  the chip reads 0x0D, so the only difference is bit 4. (Bit naming is from that comment, not from the TRM.)
- Related values read from the chip: Predischarge Timeout 0x05 (library: x10 ms = 50 ms), Stop Delta 0x32 (library: x10 mV
  = 500 mV). What happens when the timeout expires before LD reaches the stack voltage is NOT documented in the references.
- Firmware now has `bq_cfg_service()` (mailbox g_bq_cfg_req = 0xFE7E0200 | index): index 0 writes 0x9308 = 0x1D, index 1 writes
  0x0D back. Only if the live value is the expected one; confirms CONFIG_UPDATE; always exits; verifies read-back. RAM only.

## Pre-discharge enabled and tested without a load - 2026-11-07
- First write attempt aborted safely (reason 4: no CONFIG_UPDATE seen within a fixed 10 ms while the BQ was in SLEEP);
  nothing was written. Fixed with a ~200 ms poll; after SLEEP_DISABLE the write succeeded: FET Options 0x0D -> 0x1D, read-back
  verified, CONFIG_UPDATE entered/exited, only that one byte changed across all 78 windows. RAM only (reverts on BQ reset).
- Test 2 (no load, data/pdsg_noload.csv, 30 ms, all channels): ALL_FETS_ON -> FET Status 0x18 (PDSG bit set, DSG off) at 5010 ms,
  0x1C (PDSG + DSG) 60 ms later with LD jumping 1505 -> 2624, CHG on ~1 s later, PDSG bit cleared at 6270 ms. No SCD, current
  <= 22 mA. => the PDSG hardware path is asserted by the BQ. PDSG stayed set ~1.2 s in total and overlapped DSG; why is not documented.
- Outputs returned to the safe state (ALL_FETS_OFF). Test 3 (load connected) pending the owner reconnecting the load.

## Pre-discharge timeout set to 500 ms + guarded runner + no-load dry run - 2026-11-07
- Owner data: PDSG resistor 1 kOhm; board base capacitance ~0.2 uF (RC ~0.2 ms => ~0.7 ms to charge 16 V to within 0.5 V); the real
  load's capacitance is unknown and its current starts quickly (through 1 kOhm the voltage condition needs the load to draw < ~0.5 mA).
- Wrote Predischarge Timeout (0x930E) 0x05 -> 0x32 (500 ms) via the guarded config writer (RAM only; undo = cfg index 3).
  Absolute-address diff vs the original power-up config: only 0x9308 (0x0D->0x1D) and 0x930E (0x05->0x32) differ.
- New `tools/run_load_test.py`: preconditions, SLEEP_DISABLE, logger, ALL_FETS_ON, host-side guards (current > 4.5 A,
  cell < 3.3 V, temp > 60 C, SSA, BQ opening DSG), ALWAYS ALL_FETS_OFF in a finally block, CSV dump, summary.
- Dry run with NO load (data/dryrun_noload_500ms.csv): PDSG seen at 3660 ms, DSG at 4170 ms (gap 510 ms = the timeout).
  **LD stayed flat (1505-1507 raw) for the whole PDSG phase and only jumped (to 2648) when DSG closed**; same pattern in the
  earlier 50 ms run (LD flat, jump at DSG close). With 1 kOhm + 0.2 uF LD should have risen within ~1 ms if the PDSG path
  conducts. So (a) DSG is being closed by the TIMEOUT, not the voltage condition; (b) the PDSG path does not appear to
  charge LD as seen by the BQ. Cause NOT determined (hardware path? LD node? update lag?) - scope on LD and the PDSG gate needed.
- Load test NOT run: LD read 1505 (no load attached).

## Pre-discharge made timing-only - 2026-11-07
- Owner: LD is measured on the near side of the ideal diodes, before the PDSG path pushes voltage, so LD cannot confirm
  pre-discharge charging; use timing only. (This retracts my previous inference that "the PDSG path does not charge LD" -
  the flat LD trace is explained by where LD is measured; the PDSG hardware path itself is untested by LD.)
- Wrote Predischarge Stop Delta (0x930F) 0x32 -> 0x00 (voltage condition disabled per the supplied description; RAM only; undo =
  cfg index 5). On-chip now: FET Options 0x1D, Timeout 0x32 (500 ms), Stop Delta 0x00.
- Run data/loadtest_timing_only.csv: both off -> (3584 ms) CHG+PDSG on -> (+466 ms, i.e. the 500 ms timeout within 30 ms sampling)
  DSG on -> PDSG off ~480 ms later. DSG stayed on 20 s; max |current| 22 mA, no SSA. The runner printed "no load" and the BQ
  measured ~no current, so the load was NOT attached: this was effectively a no-load repeat that confirms timing-only operation.
- Outputs returned to OFF by the runner; sleep disable lapses and is re-sent by the runner each run.

## Load test run 2 - load attached, pre-discharge timing-only (500 ms) - 2026-11-07
- data/loadtest_run2.csv (625 samples, 30 ms, 0 read errors). Runner pre-check: LD 1170 (load attached), all FETs off, no faults.
- Attempt 1: ALL_FETS_ON -> CHG+PDSG on at 3583 ms; current during the 500 ms PDSG phase ~0 (+-15 mA, the ADC step);
  at 4080 ms (497 ms later = timeout expiry / DSG close) **SCD flagged (Alarm SSA, FET all off)**; that single sample read
  -1092 mA (the only |current| > 25 mA in the run). DSG never turned on.
- The BQ auto-recovered ~5.7 s later and RETRIED because ALL_FETS_ON was still in force: attempt 2 PDSG at 9810 ms, SCD again at
  ~10290 ms; SSA cleared again at ~15.8 s. Runner then ended: ALL_FETS_OFF sent; final state all FETs off, Safety A/B/C = 0.
- Conclusion so far: timing-only pre-discharge (1 kOhm, 500 ms) did NOT prevent the SCD trip with this load. Cause not determined
  (load capacitance too large for 1 kOhm/500 ms? PDSG path not delivering? load drawing current at DSG close?). 1 kOhm charges C to
  within 0.5 V of 16 V in ~3.5*R*C: 10 uF 35 ms, 100 uF 0.35 s, 470 uF 1.6 s, 1000 uF 3.5 s (> the 2.55 s maximum timeout).

## SCD threshold raised to 100 A (owner decision) - 2026-11-07
- Owner: the power supply has its own short-circuit protection, so asked for a 100 A limit.
- Wrote SCD Threshold (0x9286) index 0 -> 5 (library/doc table: index 5 = 100 mV = 100 A at 1 mOhm) via the guarded writer
  (cfg index 6; undo = index 7; RAM only; read-back verified). SCD Delay (0x9287) unchanged (0x02).
- Consequence: with only SCD and COV enabled, the BQ now has effectively no discharge-overcurrent protection below 100 A.
  The runner's host-side guard (4.5 A, seconds latency) is the only other net. A 1 mOhm sense resistor at 100 A dissipates 10 W
  (I^2 R) - its rating, the FETs and the traces were not checked by me.
- On-chip changes vs the original power-up config now: FET Options 0x0D->0x1D, Predischarge Timeout 0x05->0x32,
  Predischarge Stop Delta 0x32->0x00, SCD Threshold 0x00->0x05. All RAM only; they revert on any BQ reset / power cycle.

## Load test run 3 - load attached, SCD 100 A, pre-discharge timing-only 500 ms - 2026-11-07
- data/loadtest_run3.csv (923 samples, 30 ms, 0 read errors). Runner: LD 1170 (load attached), all FETs off, no faults.
- Sequence: CHG+PDSG on at 3494 ms -> DSG on at 3960 ms (466 ms gap, = the 500 ms timeout within sampling) -> PDSG off ~510 ms
  later -> DSG stayed on 20.5 s -> runner ALL_FETS_OFF. **No SCD / no SSA, no read errors.**
- Current while DSG on: mean -42.6 mA, min -105 mA, max 0 (before: mean -1.6 mA). So the load drew only tens of mA; the expected
  amps-level draw ("two parter", sudden jump) did NOT occur in the window - the second part was apparently not running.
- Inference (assumes the same inrush each time and the index->mV table is right): run 2 tripped SCD at 10 A and run 3 did not at
  100 A, so the DSG-close inrush peaked between 10 A and 100 A (above the SCD delay). The CC current reading never shows the spike.
- Open: a real high-current run needs the load's second stage running while DSG is on (use --duration 60).

## SCD delay sweep at a fixed 10 A threshold, load attached - 2026-11-07
- tools/sweep_scd_delay.py; aggregated CSV data/scd_delay_sweep.csv; per-trial detail data/scd_sweep/trial_*.csv. Load = the same
  attached load as runs 3/4 (draws only tens of mA; its second/high-current stage was NOT running). PDSG timing-only 500 ms.
- Result: raw 2..10 (library: ~15-150 us) -> TRIP (1 trial each, SCD at 480-510 ms after PDSG = at DSG close);
  **raw 11 (library: 150-165 us) -> PASS 3/3** (trials 10-12). Library unit ~15 us/count; its two formulas differ by one count.
- Inference: the DSG-close inrush stays above 10 A for roughly 135-165 us. Raw 2..10 were each tested once, so the boundary is not
  statistically tight; raw 11 has no margin against raw 10.
- Sweep CSV quirk: PASS rows carry the note "DSG off at end of window" - an artifact (the script sends ALL_FETS_OFF before its final read).
- Script restored the previous state: SCD delay raw 2, threshold index 5 (100 A) - verified (RESTORE OK). Not yet applied: 10 A + a chosen
  delay (owner wants 10 A as the SCD limit; delay choice 11 vs 13/14 pending).
