### Configuration Ranges and Limits

| Protection Feature | Configuration Register Location | Available Range | Resolution / Steps |
| --- | --- | --- | --- |
| **Cell Overvoltage (COV)** | `Protections:COV:Threshold` | 1.012 V to 5.566 V | 50.6 mV steps |
| **Cell Undervoltage (CUV)** | `Protections:CUV:Threshold` | 1.012 V to 4.048 V | 50.6 mV steps |
| **Overcurrent in Charge (OCC)** | `Protections:OCC:Threshold` | 4 mV to 124 mV | 2 mV steps |
| **Overcurrent in Discharge (OCD1, OCD2)** | `Protections:OCD1:Threshold`, `Protections:OCD2:Threshold` | 4 mV to 200 mV | 2 mV steps |
| **Short Circuit in Discharge (SCD)** | `Protections:SCD:Threshold` | 10 mV to 500 mV | 16 discrete thresholds |
| **Overtemp (Cells & Internal) (OTC, OTD, OTINT)** | `Protections:OTC:Threshold`, `OTD:Threshold`, `OTINT:Threshold` | -40°C to 120°C | 1°C steps |
| **Undertemp (Cells & Internal) (UTC, UTD, UTINT)** | `Protections:UTC:Threshold`, `UTD:Threshold`, `UTINT:Threshold` | -40°C to 120°C | 1°C steps |
| **Overtemp FET (OTF)** | `Protections:OTF:Threshold` | 0°C to 150°C | 1°C steps

 |

### Where Events are Recorded

When safety and protection thresholds are breached, the device records the events across several tiered status registers accessible via direct subcommands:

* **Safety Alert Registers (A/B/C):** Flags transient alerts immediately when a threshold is crossed, before the programmed delay timer (e.g., `COV_DLY`) expires.
* **Safety Status Registers (A/B/C):** Flags full faults once the delay timer expires. Setting a bit in these registers triggers the autonomous FET shutdown (if `FET_EN` is enabled).
* **PF Status Registers (A/B/C/D):** Records severe Permanent Failures (e.g., Safety Overvoltage, Safety Undervoltage, FET failures). Setting a bit here can permanently disable the pack and trigger the chemical FUSE pin.
* **Alarm & Alarm Raw Status (0x62, 0x64):** An aggregated master register that maps specific safety faults, alerts, or PF statuses to the hardware `ALERT` pin to interrupt the host processor.

