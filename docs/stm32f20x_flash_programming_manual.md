# PM0059: STM32F205/215 and STM32F207/217 Flash Programming Manual

## 1. Introduction & Glossary

This manual describes how to program the Flash memory of STM32F205/215 and STM32F207/217 microcontrollers (referred to as STM32F20x and STM32F21x).

Embedded Flash memory can be programmed using:

* **In-Circuit Programming (ICP):** Updates the entire Flash memory contents using JTAG, SWD, or the bootloader. Offers quick design iterations.
* **In-Application Programming (IAP):** Uses any communication interface to download data into memory while the application is running. Requires a previously programmed ICP bootloader.

### Glossary
* **JTAG-DP / SWD-DP:** Debug ports (5-pin JTAG or 2-pin Serial Wire Debug).
* **Word / Half-word / Byte / Double word:** 32-bit / 16-bit / 8-bit / 64-bit data lengths.
* **I-Code bus:** Connects CPU Instruction bus to Flash instruction interface (Prefetch performed here).
* **D-Code bus:** Connects CPU D-Code bus to Flash data interface.
* **Option bytes:** Product configuration bits stored in Flash.
* **OBL:** Option byte loader.

## 2. Flash Memory Interface

### 2.1 Main Features
* Capacity up to 1 Mbyte.
* 128-bit wide data read.
* Byte, half-word, word, and double word write.
* Sector and mass erase.
* Read/write protections.
* Prefetch on I-Code.
* 64 cache lines of 128 bits on I-Code; 8 cache lines of 128 bits on D-Code.

### 2.2 Memory Organization
The Flash memory is organized into:
1. **Main memory block:** 4 sectors of 16 Kbytes, 1 sector of 64 Kbytes, and 7 sectors of 128 Kbytes.
2. **System memory:** 30 Kbytes used to boot the device in System memory boot mode (contains factory ST bootloader).
3. **OTP area:** 528 bytes of One-Time Programmable memory for user data + 16 additional lock bytes.
4. **Option bytes:** 16 bytes configuring read/write protections, BOR level, and watchdog settings.

| Block Name | Base Addresses | Size | 
| ----- | ----- | ----- | 
| **Main Memory (Sector 0-11)** | `0x0800 0000` - `0x080F FFFF` | Up to 1 Mbyte | 
| Sector 0 - 3 | `0x0800 0000` - `0x0800 FFFF` | 16 Kbyte each | 
| Sector 4 | `0x0801 0000` - `0x0801 FFFF` | 64 Kbyte | 
| Sector 5 - 11 | `0x0802 0000` - `0x080F FFFF` | 128 Kbyte each | 
| **System memory** | `0x1FFF 0000` - `0x1FFF 77FF` | 30 Kbyte | 
| **OTP area** | `0x1FFF 7800` - `0x1FFF 7A0F` | 528 bytes | 
| **Option bytes** | `0x1FFF C000` - `0x1FFF C00F` | 16 bytes | 

### 2.3 Read Interface & ART Accelerator™
To correctly read from Flash, the wait states (`LATENCY`) in the `FLASH_ACR` register must match the CPU clock (HCLK) and supply voltage. 
*Note: Prefetch must be disabled when the supply voltage is below 2.1 V.*

**ART Accelerator™ (Adaptive Real-Time):**
* **Instruction Prefetch:** Reads the next 128-bit line sequentially. Enable via `PRFTEN`.
* **Instruction Cache (I-Cache):** 64 lines of 128 bits caching branch targets. Enable via `ICEN`.
* **Data Cache (D-Cache):** 8 rows of 128 bits for literal pools. Enable via `DCEN`. *(Note: Data in the user configuration sector is not cacheable).*

### 2.4 Erase and Program Operations

**Prerequisites:** 
* CPU Clock (HCLK) must be ≥ 1 MHz.
* Any read attempt during a write/erase operation will stall the bus until completion.

#### 2.4.1 Unlocking the Flash Control Register
After reset, `FLASH_CR` is locked.
1. Write `0x45670123` to `FLASH_KEYR` (KEY1).
2. Write `0xCDEF89AB` to `FLASH_KEYR` (KEY2).
   *Lock it again by setting the `LOCK` bit in `FLASH_CR`.*

#### 2.4.2 Program/Erase Parallelism & Voltage Limits
The Parallelism size (`PSIZE` in `FLASH_CR`) must strictly match your operating voltage. Mismatches will trigger a Programming Parallelism Error (`PGPERR`).

| Voltage Range | External VPP Used? | Max Parallelism | PSIZE[1:0] Config | Access Size |
| ----- | ----- | ----- | ----- | ----- |
| **2.7 - 3.6 V** | Yes | **x64** | `11` | Double Word |
| **2.7 - 3.6 V** | No | **x32** | `10` | Word |
| **2.4 - 2.7 V** | No | **x16** | `01` | Half-Word |
| **2.1 - 2.4 V** | No | **x8** | `00` | Byte |
| **1.8 - 2.1 V** | No | **x8** | `00` | Byte |

#### 2.4.3 Erase Sequences
**Sector Erase:**
1. Check `BSY` bit in `FLASH_SR` is 0.
2. Set `SER` bit and select sector via `SNB` field in `FLASH_CR`.
3. Set `STRT` bit in `FLASH_CR`.
4. Wait for `BSY` to clear.

**Mass Erase (Main memory only):**
*(Does not affect OTP or Option Bytes)*
1. Check `BSY` bit is 0.
2. Set `MER` bit in `FLASH_CR`.
3. Set `STRT` bit in `FLASH_CR`.
4. Wait for `BSY` to clear.

#### 2.4.4 Programming Sequence
1. Check `BSY` bit is 0.
2. Set `PG` bit in `FLASH_CR`.
3. Write data to memory address (access size must match `PSIZE`).
4. Wait for `BSY` to clear.

*Note: Successive write operations are possible without erase when changing bits from '1' to '0'. Writing '0' to '1' requires an erase first.*

### 2.5 Interrupts & Error Flags
Interrupts can be triggered upon completion (`EOPIE`) or upon error (`ERRIE`). Errors flag in the `FLASH_SR` register:
* `PGAERR`: Alignment error (crossing 128-bit row).
* `PGPERR`: Parallelism error (access size mismatched with PSIZE/Voltage).
* `PGSERR`: Sequence error (e.g., writing without setting PG bit).
* `WRPERR`: Write protection error.
* `OPERR`: General operation error.

### 2.6 Option Bytes & Protection

#### Option Byte Organization (`0x1FFF C000`)
* `[31:16]` - Reserved
* `[15:8]` - **RDP** (Read Protection Level)
* `[7]` - **nRST_STDBY** (0: Reset generated when entering Standby)
* `[6]` - **nRST_STOP** (0: Reset generated when entering Stop)
* `[5]` - **WDG_SW** (0: Hardware watchdog, 1: Software watchdog)
* `[3:2]` - **BOR_LEV** (Brownout Reset Thresholds):
  * `00`: BOR Level 3 (2.70 to 3.60 V)
  * `01`: BOR Level 2 (2.40 to 2.70 V)
  * `10`: BOR Level 1 (2.10 to 2.40 V)
  * `11`: BOR off (1.80 to 2.10 V)
* `0x1FFF C008 [27:16]` - **nWRP** (Write protections for Sectors 0-11, 0 = Protected).

#### Read Protection (RDP) Levels:
* **Level 0 (`0xAA`):** No protection.
* **Level 1 (Any value except `0xAA` or `0xCC`):** Memory read protection. Disables access via debug/RAM/bootloader. *Downgrading from Level 1 to Level 0 triggers an automatic Mass Erase of main Flash and backup SRAM.*
* **Level 2 (`0xCC`):** Chip read protection. Irreversible. JTAG/SWD permanently disabled. 

**Unlocking Option Bytes:**
1. Clear `OPTLOCK` in `FLASH_OPTCR`.
2. Write `0x0819 2A3B` then `0x4C5D 6E7F` to `FLASH_OPTKEYR`.

### 2.7 One-Time Programmable (OTP) Bytes
* Address: `0x1FFF 7800` to `0x1FFF 7A0F`
* Divided into 16 blocks (`OTP0` to `OTP15`). Each block contains 32 bytes (split across two 16-byte address lines, e.g., OTP0 is at `0x1FFF 7800` and `0x1FFF 7810`).
* 1 Lock block of 16 bytes (`LOCKB0` to `LOCKB15`).
* Programming `0x00` into `LOCKBi` permanently locks `OTPi`. (Lock bytes must only be `0xFF` or `0x00`).

## 3. Flash Interface Registers (`0x4002 3C00` Base)

### 3.1 `FLASH_ACR` (Access Control Register) - Offset `0x00`
* `[12] DCRST`: Data cache reset.
* `[11] ICRST`: Instruction cache reset.
* `[10] DCEN`: Data cache enable.
* `[9] ICEN`: Instruction cache enable.
* `[8] PRFTEN`: Prefetch enable.
* `[2:0] LATENCY`: Wait states (000 = 0 WS ... 111 = 7 WS).

### 3.2 `FLASH_KEYR` (Key Register) - Offset `0x04`
* Write `0x45670123` then `0xCDEF89AB` to unlock `FLASH_CR`.

### 3.3 `FLASH_OPTKEYR` (Option Key Register) - Offset `0x08`
* Write `0x08192A3B` then `0x4C5D6E7F` to unlock `FLASH_OPTCR`.

### 3.4 `FLASH_SR` (Status Register) - Offset `0x0C`
* `[16] BSY`: Busy (Flash operation in progress).
* `[7] PGSERR`: Programming sequence error.
* `[6] PGPERR`: Programming parallelism error.
* `[5] PGAERR`: Programming alignment error.
* `[4] WRPERR`: Write protection error.
* `[1] OPERR`: Operation error.
* `[0] EOP`: End of operation (cleared by writing 1).

### 3.5 `FLASH_CR` (Control Register) - Offset `0x10`
*(Locked after reset. BSY must be 0 to write).*
* `[31] LOCK`: Write 1 to lock `FLASH_CR`.
* `[25] ERRIE`: Error interrupt enable.
* `[24] EOPIE`: End of operation interrupt enable.
* `[16] STRT`: Start erase operation.
* `[9:8] PSIZE`: Program size (00=x8, 01=x16, 10=x32, 11=x64).
* `[6:3] SNB`: Sector number to erase (0000=Sec 0 ... 1011=Sec 11).
* `[2] MER`: Mass erase.
* `[1] SER`: Sector erase.
* `[0] PG`: Programming mode.

### 3.6 `FLASH_OPTCR` (Option Control Register) - Offset `0x14`
* `[27:16] nWRP[11:0]`: Not write protect (0 = Active/Protected).
* `[15:8] RDP[7:0]`: Read protect level.
* `[7] nRST_STDBY`: 0 = Reset on Standby.
* `[6] nRST_STOP`: 0 = Reset on Stop.
* `[5] WDG_SW`: 0 = Hardware WDG, 1 = Software WDG.
* `[3:2] BOR_LEV`: Brownout reset thresholds.
* `[1] OPTSTRT`: Option start (trigger option byte erase/program).
* `[0] OPTLOCK`: Option lock (Write 1 to lock, unlock via `FLASH_OPTKEYR`).