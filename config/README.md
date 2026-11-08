# config/ — the files you edit

| File | What it controls |
|---|---|
| `bms_settings.csv` | The BQ76952 settings the MCU applies **at every start-up** and re-checks every few seconds (so a BMS power cycle can't leave the BQ on default settings). One row per register: address, size, value, and a plain-language meaning. |
| `bms_options.csv` | How the auto-configuration behaves (on/off, first-check delay, check period, keep-awake interval). |

**To change a setting**

1. Edit the CSV (keep the `meaning` / `notes` columns up to date for the next person).
2. `python3 tools/bms.py config apply`   (validates → regenerates `Inc/bms_config_generated.h` → builds → flashes → verifies)
3. `python3 tools/bms.py status`   (about 15 s later every setting should say `MATCH`)

The generator refuses security-key registers outright and requires `FUSE-OK` in the `ack` column for fuse / permanent-failure registers.
Never edit `Inc/bms_config_generated.h` by hand — it is overwritten.

Full instructions, the meaning of every current setting, and troubleshooting: [`../docs/User-Guide.md`](../docs/User-Guide.md) (section 6).
