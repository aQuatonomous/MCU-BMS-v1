# data/ — results written by the tools

New files are created by the tools with a timestamp or the name you give them. Nothing here is needed to build or run the firmware.
Pairs named `X.csv` + `X_bq_config.csv` come from `tools/read_bms_csv.py` (live registers + the BQ settings read-out).

| Files | What they are |
|---|---|
| `status/latest.csv` (+`_bq_config`) | written by `python3 tools/bms.py status` each time (overwritten) |
| `recheck_20261108*` | the connection re-check at the start of 2026-11-08 (BQ had been power-cycled overnight: settings at defaults) |
| `bms_readout_*`, `bq_config_*`, `limits_readout*`, `balancing_readout*`, `pf_readout*` | one-off register / configuration read-outs (limits, balancing, permanent-fail/fuse) |
| `before_*` / `after_*` / `pre_*` / `post_*` | before-and-after snapshots around each change we made (FET enable, ALL_FETS_ON, RST_SHUT, PDSG, timeout, SCD, COV/CUV, VCell Mode…) |
| `load_run1.csv`, `loadtest_run2.csv`, `loadtest_run3.csv`, `loadtest_run4.csv`, `loadtest_timing_only.csv` | load-test logs (run 1: SCD tripped, no pre-discharge; run 2: tripped with pre-discharge; runs 3–4: passed with the SCD limit at 100 A) |
| `retry_noload.csv`, `pdsg_noload.csv`, `dryrun_noload_500ms.csv` | no-load turn-on logs (pre-discharge behaviour) |
| `logger_selftest.csv`, `logger_lean_test.csv`, `stability_check.csv`, `state_check.csv`, `now_state.csv` | logger tests and state observations |
| `scd_delay_sweep.csv` | the 12-trial SCD delay sweep at 10 A (raw 2…10 TRIP, raw 11 PASS 3/3) — **reconstructed** after the original was overwritten on 2026-11-08; per-trial detail in `scd_sweep/20261107_run1_10A/` |
| `scd_delay_sweep_raw14_check_20261108.csv` | one-trial check at raw 14 (no load) used to test the new tooling; detail in `scd_sweep/20261108_raw14_check/` |
| `scd_delay_sweep_<timestamp>.csv`, `scd_sweep/<timestamp>/` | every future sweep (a new file each time) |

How to read the columns: `docs/User-Guide.md` section 5. Units: cells mV, current mA (already corrected, negative = discharge); stack/pack/LD are raw counts (scaling unverified).
