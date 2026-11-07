# STM32F205xx & STM32F207xx Datasheet Summary

## 1. Core & Architecture
* **Core:** Arm® 32-bit Cortex®-M3 CPU.
* **Max Clock Speed:** 120 MHz.
* **Performance:** 150 DMIPS / 1.25 DMIPS/MHz.
* **Acceleration:** ART Accelerator™ (Adaptive Real-Time) allows 0-wait state execution from Flash memory (requires proper Wait State configuration).

## 2. Memory Specifications
* **Flash Memory:** Up to 1 Mbyte (Note: 'E' variants like STM32F205RET6 are strictly 512 Kbytes. Safe range: `0x0800 0000` to `0x0807 FFFF`).
* **SRAM:** Up to 128 Kbytes System SRAM + 4 Kbytes Backup SRAM.
* **OTP Memory:** 512 bytes (One-Time Programmable).
* **External Memory:** Flexible Static Memory Controller (FSMC) supports CF, SRAM, PSRAM, NOR, and NAND.

## 3. High-Level Memory Map
| Region | Base Address | End Address | Description |
| :--- | :--- | :--- | :--- |
| **Code (Flash)** | `0x0000 0000` | `0x1FFF FFFF` | Main Flash (starts `0x0800 0000`), System Memory (bootloader), OTP. |
| **SRAM** | `0x2000 0000` | `0x3FFF FFFF` | Internal Data RAM. |
| **Peripherals** | `0x4000 0000` | `0x5FFF FFFF` | APB1, APB2, AHB1, AHB2 peripherals (e.g., RCC @ `0x4002 3800`, TIM2 @ `0x4000 0000`). |
| **FSMC** | `0x6000 0000` | `0x9FFF FFFF` | External memory interfaces. |
| **Cortex-M3** | `0xE000 0000` | `0xFFFF FFFF` | Core registers (NVIC, SysTick, SCB). |

## 4. Clocks & Power
* **Voltage Supply:** 1.8 V to 3.6 V.
* **Bus Limits:** 
  * **AHB (SYSCLK):** 120 MHz Max
  * **APB2 (High-Speed):** 60 MHz Max
  * **APB1 (Low-Speed):** 30 MHz Max
* **Flash Latency (Wait States):** Must be configured in `FLASH_ACR` based on SYSCLK and Voltage. (e.g., 120 MHz at 3.3V = **3 Wait States**).
* **Oscillators:**
  * External High-Speed (HSE): 4 to 26 MHz crystal.
  * Internal High-Speed (HSI): 16 MHz factory-trimmed RC.
  * Internal/External Low-Speed (LSI/LSE): 32 kHz / 32.768 kHz for RTC.

## 5. Key Peripherals (STM32F205xx Series)
* **Timers:** Up to 12x 16-bit, 2x 32-bit (TIM2, TIM5) running up to 120 MHz.
* **Analog:** 3x 12-bit ADCs (up to 24 channels, 6 MSPS interleaved), 2x 12-bit DACs.
* **Communication:** 
  * Up to 6x USART/UARTs
  * Up to 3x SPIs / 3x I2Cs
  * 2x CAN interfaces
  * USB OTG HS/FS
* **DMA:** 16-stream controller with centralized FIFOs.

## 6. Pinout & Alternate Functions (AF)
Pins are highly multiplexed. A standard LQFP64 package (like the RET6) allows pins like `PA0` to `PC15` to be remapped internally to up to 16 different Alternate Functions (AF0 to AF15), connecting physical pins directly to internal Timers, USARTs, I2C, SPI, or USB hardware. (Reference Datasheet Table 10 for exact mappings).