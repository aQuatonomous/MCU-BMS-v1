## BMS MCU V1
# Goal
We are trying to build a program on an STM32F205 that connects to a Battery Management System Texas Instruments BQ7695202 IC. This has been built on a printed circuit board. 

# Project Status and Goals
- The board is built, but we need to program the STM32
- We have not applied power to the BMS IC yet, only the MCU, so there is no data that can be transferred over yet.
- We are looking to clock our USB lines using PLL on an external crystal oscillator. This crystal oscillator opperates at 16 MHz, we are looking to clock USB lines at 48M MHz
- Until we power on the BMS we are limited to what we can read from it. For now we should ensure we can reach the memory for each of the I/O registers.


# Notes
- Pin Mapping is available in /Users/logancalder/Documents/Aquatonomous/BMS-MCU-V1/docs
- STM32 is connected over STM32Cube through ST-Link V2. This is available in run and debug under STM32Cube Devices. Serial ID: 37FF71064E573436D8421743

# Documentation (added 2026-11-08)
- **[docs/User-Guide.md](docs/User-Guide.md)** — step-by-step: check the system, upload (flash) firmware, read data, write BQ settings, run studies, troubleshooting.
- **[config/](config/README.md)** — the settings the MCU applies to the BQ at every start-up (edit the CSV, then `python3 tools/bms.py config apply`).
- **`python3 tools/bms.py status`** — one command that checks every connection. `python3 tools/bms.py --help` lists the rest.
- [data/README.md](data/README.md) describes the result files; [docs/Claude-notes.md](docs/Claude-notes.md) is the running lab notebook.
- Note: the *Project Status* bullets above predate the bring-up (the BMS is powered, the clock/USB core and I2C link work, the BQ is read and configured over I2C).
