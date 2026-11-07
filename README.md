## BMS MCU V1
# What we are doing
We are trying to build a program on an STM32F205 that connects to a Battery Management System Texas Instruments BQ7695202 IC. This has been built on a printed circuit board. 

# Project Status and Goals
- The board is built, but we need to program the STM32
- We have not applied power to the BMS IC yet, only the MCU, so there is no data that can be transferred over yet.
- We are looking to clock our USB lines using PLL on an external crystal oscillator. This crystal oscillator opperates at 16 MHz, we are looking to clock USB lines at 48M MHz
- Until we power on the BMS we are limited to what we can read from it. For now we should ensure we can reach the memory for each of the I/O registers.


# Notes
- Pin Mapping is available in /Users/logancalder/Documents/Aquatonomous/BMS-MCU-V1/docs
- STM32 is connected over STM32Cube through ST-Link V2. This is available in run and debug under STM32Cube Devices. Serial ID: 37FF71064E573436D8421743
