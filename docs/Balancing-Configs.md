| Configuration Parameter | Description | Range / Default |
| --- | --- | --- |
| **Balancing Configuration** | Master bit-mask controlling balancing allowances:<br>

<br>• `CB_CHG`: Allow while charging (current above threshold)<br>

<br>• `CB_RLX`: Allow in relax conditions (current below threshold)<br>

<br>• `CB_SLEEP`: Allow during SLEEP mode<br>

<br>• `CB_NOSLEEP`: Prevent entering SLEEP while balancing is active<br>

<br>• `CB_NO_CMD`: Block host-controlled balancing commands | Default: 0x00 (All disabled) |
| **Cell Balance Max Cells** | Limits the maximum number of cells that can autonomously balance simultaneously to prevent excessive power dissipation. | 0 to 16 cells (Default: 1) |
| **Cell Balance Interval** | The time interval before the algorithm recalculates which cells to balance, or before a host-commanded balance times out. | 1 to 255 s (Default: 20 s) |
| **Cell Balance Min Cell V (Charge)** | The minimum cell voltage required for autonomous balancing to activate while the pack is charging. | 0 to 5000 mV (Default: 3900 mV) |
| **Cell Balance Min Delta (Charge)** | The required voltage difference between the highest and lowest cells to trigger autonomous balancing during charge. | 0 to 255 mV |
| **Cell Balance Stop Delta (Charge)** | The target voltage difference (hysteresis) where balancing stops during charge, preventing over-balancing. | 0 to 255 mV (Default: 20 mV) |
| **Cell Balance Min Cell V (Relax)** | The minimum cell voltage required for autonomous balancing to activate while the pack is in a relaxed state. | 0 to 5000 mV (Default: 3900 mV) |
| **Cell Balance Min Delta (Relax)** | The required voltage difference between the highest and lowest cells to trigger autonomous balancing during relax. | 0 to 255 mV (Default: 40 mV) |
| **Cell Balance Stop Delta (Relax)** | The target voltage difference (hysteresis) where balancing stops during relax. | 0 to 255 mV |
| **Max Internal Temp** | Disables all cell balancing if the device's internal die temperature exceeds this value. | -128 to 127 °C (Default: 70 °C) |
| **Min Cell Temp / Max Cell Temp** | Disables all cell balancing if the measured cell temperature falls below the minimum or exceeds the maximum limit. | -128 to 127 °C |
| **Power Config (`CB_LOOP_SLOW`)** | Adjusts the ADC scan loop speed (Full, Half, Quarter, or Eighth) while balancing is active. Slower scans increase the balancing duty-cycle, as balancing is paused while cells are actively measured. | Full to Eighth speed (Default: 0 - Full)

### Balancing Configuration Register

The bit mapping for the `Settings:Cell Balancing Config:Balancing Configuration` register is located in TRM Section 13.3.11.1. It is an 8-bit register (Type H1) mapped as follows:

| Bit | Field |
| --- | --- |
| 7–5 | RSVD_0 (Reserved) |
| 4 | CB_NO_CMD |
| 3 | CB_NOSLEEP |
| 2 | CB_SLEEP |
| 1 | CB_RLX |
| 0 | CB_CHG

 |

### Power Config Register Decode (0x2982)

The `Settings:Configuration:Power Config` register is a 16-bit register (Type H2). To determine the value of the `CB_LOOP_SLOW` field (which occupies bits 5 and 4), we convert the hexadecimal value `0x2982` into binary:

* **0x2** (Bits 15–12): `0010`
* **0x9** (Bits 11–8): `1001`
* **0x8** (Bits 7–4): `1000`
* **0x2** (Bits 3–0): `0010`

Looking specifically at the third nibble (`0x8` / `1000`), the bit breakdown is:

* Bit 7 (OTSD): 1
* Bit 6 (FASTADC): 0
* Bit 5 (CB_LOOP_SLOW_1): 0
* Bit 4 (CB_LOOP_SLOW_0): 0

Because bits 5 and 4 are both `0`, the `CB_LOOP_SLOW` field is exactly 0 (which configures the measurement loop to run at full speed during balancing). Your read value of `0x2982` exactly matches the BQ76952 factory default state for this register.