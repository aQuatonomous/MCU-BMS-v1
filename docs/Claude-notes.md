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
