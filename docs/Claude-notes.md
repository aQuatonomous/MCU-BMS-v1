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
