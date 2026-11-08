#!/usr/bin/env python3
"""SCD delay sweep at a FIXED 10 A short-circuit threshold, with the real load attached.

Per trial: all FETs off -> wait for the BQ's SCD/safety flags to clear -> write SCD Delay (0x9287) = raw N ->
PDSG(500 ms) -> DSG with the load -> watch ~3 s after DSG closes -> PASS (DSG stays on, no safety flag) or TRIP
(Alarm SSA set) -> ALL_FETS_OFF. Starts at raw 2 (the current value) and steps +1 (library: ~15 us per count; the
two library formulas differ by one count, so both interpretations are written to the CSV). Stops when a value
PASSES 3 times in a row (a passing value that later trips is treated as flaky and the sweep keeps stepping up),
or at raw 31 (library maximum). Restores the owner's previous setting (delay raw 2, threshold 100 A) at the end.
Aggregated output: data/scd_delay_sweep.csv ; per-trial detail: data/scd_sweep/trial_*.csv
Usage: python3 -I tools/sweep_scd_delay.py [--start 2] [--max 31] [--confirm 3]
"""
import argparse, csv, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_load_test as L

CFG, SCDD = 0xFE7E0200, 0xFE7E0300
W, A, words = L.w32, L.addr, L.words
OUT = os.path.join(L.ROOT, "data", "scd_delay_sweep.csv")
DET = os.path.join(L.ROOT, "data", "scd_sweep")
COLS = ["trial", "scd_delay_raw", "delay_us_if_(raw-1)x15", "delay_us_if_rawx15", "threshold_mV", "threshold_A",
        "result", "scd_tripped", "dsg_closed", "ms_pdsg_to_dsg", "ms_pdsg_to_trip", "peak_abs_current_mA", "min_cell_mV", "note"]


def say(*a): print(time.strftime("%H:%M:%S"), *a, flush=True)


def cfg_write(base, value):
    req = A("g_bq_scdd_req") if base == SCDD else A("g_bq_cfg_req")
    W(req, base | value); time.sleep(5)
    res = words(A("g_bq_cfg_res"), 6)           # done, reason, addr, before, after, cfgupdate_seen
    return res


def cmd(i, wait=3):
    W(A("g_bq_cmd_req"), L.CMD_MAGIC | i); time.sleep(wait)


def wait_clear(max_s=45):
    t0 = time.time()
    while time.time() - t0 < max_s:
        r, _ = L.snapshot("sw")
        saf = [int(r[k]["value_hex"], 16) for k in ("SafetyStatusA", "SafetyStatusB", "SafetyStatusC")]
        fs = int(r["FETStatus"]["value_hex"], 16)
        if not any(saf) and not (fs & 0x0F): return True, r
        time.sleep(1)
    return False, r


def trial(n, raw):
    row = {"trial": n, "scd_delay_raw": raw, "delay_us_if_(raw-1)x15": (raw - 1) * 15, "delay_us_if_rawx15": raw * 15,
           "threshold_mV": 10, "threshold_A": 10, "result": "ERROR", "scd_tripped": "", "dsg_closed": "", "ms_pdsg_to_dsg": "",
           "ms_pdsg_to_trip": "", "peak_abs_current_mA": "", "min_cell_mV": "", "note": ""}
    cmd(1)                                              # ALL_FETS_OFF
    ok, r = wait_clear()
    if not ok: row["note"] = "SCD/safety flag did not clear in 45 s"; return row, None
    cmd(0)                                              # SLEEP_DISABLE (config writes need the BQ awake)
    res = cfg_write(SCDD, raw)
    if res[:2] != [1, 0] or res[4] != raw: row["note"] = f"delay write failed {res}"; return row, None
    W(A("g_log_period_ms"), 30); W(A("g_log_slow_div"), 1)
    W(A("g_log_req"), L.LOG_START); time.sleep(2.5)
    W(A("g_bq_fets_on_req"), L.FETON_MAGIC)
    verdict = None; rows = []
    t_p = t_d = t_s = None; t_end = time.time() + 16
    try:
        while time.time() < t_end:
            h, rows = L.tail(200)
            if not rows: continue
            for x in rows:
                t, c1, c2, c3, c16, stack, pack, ld, cc2, alarm, batt, temp, fet, flags = x
                if t_p is None and (fet & 0x08): t_p = t
                if t_d is None and (fet & 0x04): t_d = t
                if t_s is None and ((alarm >> 14) & 1): t_s = t
                cur = L.s16(cc2) if cc2 != 0xFFFF else 0
                if abs(cur) > 4500: verdict = "ABORT_CURRENT"
            if verdict: break
            if t_s is not None: break
            if t_d is not None and rows[-1][0] >= t_d + 3000: break
    finally:
        cmd(1, wait=2)                                  # ALL_FETS_OFF immediately
        W(A("g_log_req"), L.LOG_STOP); time.sleep(1)
    h, rows = L.tail(200)
    cur = [abs(L.s16(x[8])) for x in rows if x[8] != 0xFFFF]
    cells = [v for x in rows for v in x[1:5] if v != 0xFFFF and v > 1000]
    dsg_end = bool(rows and (rows[-1][12] & 0x04))
    row.update({"scd_tripped": int(t_s is not None), "dsg_closed": int(t_d is not None),
                "ms_pdsg_to_dsg": (t_d - t_p) if (t_d is not None and t_p is not None) else "",
                "ms_pdsg_to_trip": (t_s - t_p) if (t_s is not None and t_p is not None) else "",
                "peak_abs_current_mA": max(cur) if cur else "", "min_cell_mV": min(cells) if cells else ""})
    if verdict: row["result"] = verdict
    elif t_s is not None: row["result"] = "TRIP"
    elif t_d is not None: row["result"] = "PASS"; row["note"] = "" if dsg_end else "DSG off at end of window"
    else: row["result"] = "NO_DSG"; row["note"] = "neither DSG nor a trip within 16 s"
    return row, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--start", type=int, default=2); ap.add_argument("--max", type=int, default=31)
    ap.add_argument("--confirm", type=int, default=3)
    a = ap.parse_args()
    os.makedirs(DET, exist_ok=True)
    f = open(OUT, "w", newline=""); w = csv.DictWriter(f, fieldnames=COLS); w.writeheader(); f.flush()
    restore_ok = None
    try:
        say("setup: ALL_FETS_OFF, wait clear, SLEEP_DISABLE, threshold -> index 0 (10 mV = 10 A)")
        cmd(1); ok, r = wait_clear()
        if not ok: sys.exit("ABORT: flags did not clear at setup")
        cmd(0)
        res = cfg_write(CFG, 7)                         # SCD Threshold index 5 -> 0
        say("threshold write result", res, "(want [1,0,0x9286,5,0,1])")
        if res[:2] != [1, 0] or res[4] != 0: sys.exit("ABORT: could not set the 10 A threshold")
        raw, streak, n = a.start, 0, 0
        while raw <= a.max and n < 60:
            n += 1
            say(f"trial {n}: SCD delay raw {raw} ({(raw-1)*15}-{raw*15} us)")
            row, rows = trial(n, raw)
            w.writerow(row); f.flush()
            if rows:
                with open(os.path.join(DET, f"trial_{n:02d}_raw{raw:02d}.csv"), "w", newline="") as g:
                    cw = csv.writer(g); cw.writerow(["t_ms", "cell1", "cell2", "cell3", "cell16", "stack", "pack", "ld", "cc2_raw", "alarm_raw", "battery", "temp", "fet", "flags"])
                    cw.writerows(rows)
            say("   ->", row["result"], {k: row[k] for k in ("scd_tripped", "dsg_closed", "ms_pdsg_to_dsg", "ms_pdsg_to_trip", "peak_abs_current_mA")}, row["note"])
            if row["result"] == "PASS":
                streak += 1
                if streak >= a.confirm: say(f"SAFE: raw {raw} passed {streak} times in a row"); break
            elif row["result"] == "TRIP":
                streak = 0; raw += 1
            else:
                say("   unexpected result - stopping sweep"); break
        else:
            say("end of range / trial limit reached without a confirmed pass")
    finally:
        say("restore: ALL_FETS_OFF, wait clear, delay -> raw 2, threshold -> index 5 (100 A)")
        try:
            cmd(1); ok, r = wait_clear(); cmd(0)
            r1 = cfg_write(SCDD, 2); r2 = cfg_write(CFG, 6)
            say("restore results: delay", r1, "| threshold", r2)
            restore_ok = (r1[:2] == [1, 0] and r1[4] == 2 and r2[:2] == [1, 0] and r2[4] == 5)
        except Exception as e:
            say("RESTORE FAILED:", e); restore_ok = False
        say("RESTORE", "OK" if restore_ok else "FAILED - CHECK THE BQ CONFIG MANUALLY")
        f.close()
    say("CSV:", OUT)


if __name__ == "__main__":
    main()
