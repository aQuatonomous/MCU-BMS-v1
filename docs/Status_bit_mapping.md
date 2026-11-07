### Battery Status Register (0x12)

| Bit | Field | Description |
| --- | --- | --- |
| 15 | SLEEP | 1 = Device is in SLEEP mode |
| 14 | RSVD_0 | Reserved |
| 13 | SD_CMD / SDM | 1 = Shutdown due to command/pin is pending |
| 12 | PF | 1 = At least one Permanent Fail fault triggered |
| 11 | SS | 1 = At least one enabled safety fault triggered |
| 10 | FUSE | 1 = FUSE pin asserted by device or secondary protector |
| 9–8 | SEC1–SEC0 | 0 = Uninitialized, 1 = FULLACCESS, 2 = UNSEALED, 3 = SEALED |
| 7 | OTPB | 1 = Writes to OTP are blocked |
| 6 | OTPW | 1 = Writes to OTP are pending |
| 5 | COW_CHK | 1 = Device is actively performing a cell open-wire check |
| 4 | WD | 1 = Previous reset was caused by the watchdog timer |
| 3 | POR | 1 = Full reset has occurred since last exit of CONFIG_UPDATE |
| 2 | SLEEP_EN | 1 = SLEEP mode is allowed when other conditions are met |
| 1 | PCHG_MODE | 1 = Device is in PRECHARGE mode |
| 0 | CFGUPDATE | 1 = Device is in CONFIG_UPDATE mode

 |

### Alarm Bits (Registers 0x62, 0x64, 0x66)

Applies to Alarm Status (latched), Alarm Raw Status (unlatched), and Alarm Enable (mask).

| Bit | Field | Description |
| --- | --- | --- |
| 15 | SSBC | 1 = Bit set in Safety Status B() or C() |
| 14 | SSA | 1 = Bit set in Safety Status A() |
| 13 | PF | 1 = Enabled Permanent Fail fault triggered |
| 12 | MSK_SFALERT | 1 = Masked safety alert triggered |
| 11 | MSK_PFALERT | 1 = Masked Permanent Fail alert triggered |
| 10 | INITSTART | 1 = Initialization started |
| 9 | INITCOMP | 1 = Initialization completed |
| 8 | RSVD_0 | Reserved |
| 7 | FULLSCAN | 1 = Full Voltage Scan Complete |
| 6 | XCHG | 1 = CHG FET is off |
| 5 | XDSG | 1 = DSG FET is off |
| 4 | SHUTV | 1 = Stack voltage is below Shutdown Stack Voltage |
| 3 | FUSE | 1 = FUSE pin is being driven by device or protector |
| 2 | CB | 1 = Cell balancing is active |
| 1 | ADSCAN | 1 = Single Voltage ADC Scan Complete |
| 0 | WAKE | 1 = Device wakened from SLEEP mode

 |

### FET Status Register (0x7F) Bits 4–5

* **Bit 5 (DDSG_PIN):** 1 = DDSG pin is asserted (0 = not asserted)
* **Bit 4 (DCHG_PIN):** 1 = DCHG pin is asserted (0 = not asserted)

