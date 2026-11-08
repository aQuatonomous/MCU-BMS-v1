#!/usr/bin/env python3
"""Guarded load-test runner (SWD + the firmware's one-shot mailboxes).

Sequence: preconditions -> SLEEP_DISABLE -> start logger -> ALL_FETS_ON -> monitor with HOST-SIDE GUARDS for
--duration seconds after DSG comes on -> ALL_FETS_OFF (always) -> stop logger -> dump CSV -> summary.
The BQ itself only has SCD and COV enabled, so these guards are the extra safety net for the run:
  trips (-> immediate ALL_FETS_OFF): |current| > --guard-current-mA, any connected cell < --guard-cell-mV,
  temperature > --guard-temp-C (0.1 K assumed), Alarm SSA bit (a safety fault), or the BQ opening DSG by itself.
Guard resolution is limited by SWD round trips (a few seconds); it is NOT a fast hardware protection.
Usage: python3 -I tools/run_load_test.py [--require-load] [--duration 20] [--period 30]
"""
import argparse, csv, os, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dump_log_csv as D

ROOT = D.ROOT
CMD_MAGIC, FETON_MAGIC, FETEN_MAGIC = 0xFE7E0100, 0xFE7E0096, 0xFE7E0022
LOG_START, LOG_STOP = 0xFE7E1001, 0xFE7E1002
SAMPLE = struct.Struct("<I11HBB")   # t_ms, cell1,cell2,cell3,cell16,stack,pack,ld,cc2,alarm,battery,temp, fet, flags


def addr(name): return D.sym(name)[0]
def w32(a, v):
    subprocess.run([D.PROG, "-c", "port=SWD", f"sn={D.SN}", "mode=HOTPLUG", "freq=1800", "-w32", f"0x{a:08X}", f"0x{v:08X}"],
                   capture_output=True, text=True, timeout=60)
def words(a, n):
    b = D.read(a, 4 * n)
    return [int.from_bytes(b[i:i + 4], "little") for i in range(0, 4 * n, 4)]
def s16(v): return v - 65536 if v >= 32768 else v


def snapshot(tag):
    out = os.path.join(ROOT, "data", f"_{tag}.csv")
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "read_bms_csv.py"), "--samples", "1", "--out", out],
                   capture_output=True, text=True, timeout=120)
    r = {x["register"]: x for x in csv.DictReader(open(out))}
    c = {x["name"]: x for x in csv.DictReader(open(out[:-4] + "_bq_config.csv"))}
    mfg = int(c["MANUFACTURING_STATUS"]["data_hex_lsb_first"].split()[0], 16)
    return r, mfg


def tail(n=80):
    h = words(addr("g_log"), 6)
    count = h[1]
    if count == 0: return h, []
    first = max(0, count - n)
    buf = D.read(addr("g_log_samples") + first * SAMPLE.size, (count - first) * SAMPLE.size)
    rows = [SAMPLE.unpack_from(buf, i * SAMPLE.size) for i in range(count - first)]
    return h, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duration", type=float, default=20.0, help="seconds to stay on after DSG closes")
    ap.add_argument("--period", type=int, default=30)
    ap.add_argument("--require-load", action="store_true", help="abort unless LD shows a load is attached")
    ap.add_argument("--guard-current-mA", type=int, default=4500)
    ap.add_argument("--guard-cell-mV", type=int, default=3300)
    ap.add_argument("--guard-temp-C", type=float, default=60.0)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    # ---- preconditions (logger idle: probe data is live) ----
    r, mfg = snapshot("lt_pre")
    bs, fs = int(r["BatteryStatus"]["value_hex"], 16), int(r["FETStatus"]["value_hex"], 16)
    ld = int(r["LDpin"]["value_unsigned"])
    safety = [r[k]["value_hex"] for k in ("SafetyStatusA", "SafetyStatusB", "SafetyStatusC")]
    print(f"pre: Battery 0x{bs:04X} FET 0x{fs:02X} Safety {safety} Mfg 0x{mfg:04X} LD {ld} ({'LOAD ATTACHED' if ld < 1350 else 'no load'})")
    if fs & 0x0F: sys.exit("ABORT: a FET is already on")
    if any(int(x, 16) for x in safety): sys.exit("ABORT: a safety status flag is set")
    if (bs >> 13) & 1: sys.exit("ABORT: shutdown-pending bit set")
    if a.require_load and ld >= 1350: sys.exit("ABORT: --require-load but LD looks like no load is attached")
    if not (mfg & 0x10): sys.exit("ABORT: FET_EN is not set (send FET_ENABLE first)")

    A = {n: addr(n) for n in ("g_bq_cmd_req", "g_bq_cmd_res", "g_bq_fets_on_req", "g_bq_fets_on_res", "g_log_req",
                              "g_log_period_ms", "g_log_slow_div")}
    out_csv = a.out or os.path.join(ROOT, "data", time.strftime("loadtest_%Y%m%d_%H%M%S.csv"))
    logging_started = False
    verdict = "completed"
    try:
        w32(A["g_bq_cmd_req"], CMD_MAGIC | 0); time.sleep(3)          # SLEEP_DISABLE
        res = words(A["g_bq_cmd_res"], 4); print("SLEEP_DISABLE res", res, "(want [1,1,0,0x9A])")
        if res[:3] != [1, 1, 0]: sys.exit("ABORT: SLEEP_DISABLE failed")
        w32(A["g_log_period_ms"], a.period); w32(A["g_log_slow_div"], 1)
        w32(A["g_log_req"], LOG_START); logging_started = True; time.sleep(3)
        h, rows = tail(5); print("logger:", h, "last fet", hex(rows[-1][12]) if rows else None)
        w32(A["g_bq_fets_on_req"], FETON_MAGIC); print(">>> ALL_FETS_ON sent")
        t_on = None; deadline = time.time() + 14 + a.duration + 60
        while time.time() < deadline:
            h, rows = tail(120)
            if not rows: continue
            if t_on is None:
                for x in rows:
                    if x[12] & 0x04: t_on = x[0]; print(f">>> DSG on at log t={t_on} ms"); break
                if t_on is None and (rows[-1][0] > 12000 + 3000):
                    verdict = "DSG did not come on within ~12 s of ALL_FETS_ON"; print(verdict); break
            for x in rows:
                if t_on is not None and x[0] < t_on: continue
                t, c1, c2, c3, c16, stack, pack, ld_, cc2, alarm, batt, temp, fet, flags = x
                cur = s16(cc2) if cc2 != 0xFFFF else 0
                cells = [v for v in (c1, c2, c3, c16) if v != 0xFFFF and v > 1000]
                if t_on is not None and (alarm >> 14) & 1: verdict = f"GUARD: safety fault (SSA) at t={t} ms"
                elif t_on is not None and not (fet & 0x04): verdict = f"BQ opened DSG by itself at t={t} ms"
                elif abs(cur) > a.guard_current_mA: verdict = f"GUARD: current {cur} mA at t={t} ms"
                elif cells and min(cells) < a.guard_cell_mV: verdict = f"GUARD: cell {min(cells)} mV at t={t} ms"
                elif temp != 0xFFFF and (temp / 10 - 273.15) > a.guard_temp_C: verdict = f"GUARD: temp {temp/10-273.15:.1f} C at t={t} ms"
                if verdict != "completed": break
            if verdict != "completed": print(verdict); break
            if t_on is not None and rows[-1][0] >= t_on + a.duration * 1000: break
            time.sleep(0.3)
    finally:
        print(">>> ALL_FETS_OFF (always)")
        w32(addr("g_bq_cmd_req"), CMD_MAGIC | 1); time.sleep(3)
        if logging_started:
            w32(addr("g_log_req"), LOG_STOP); time.sleep(1)
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "dump_log_csv.py"), "--out", out_csv], timeout=300)
    r2, _ = snapshot("lt_post")
    fs2 = int(r2["FETStatus"]["value_hex"], 16)
    print(f"post: FET 0x{fs2:02X} (CHG={fs2&1} PDSG={(fs2>>3)&1} DSG={(fs2>>2)&1}) Safety {[r2[k]['value_hex'] for k in ('SafetyStatusA','SafetyStatusB','SafetyStatusC')]} Battery {r2['BatteryStatus']['value_hex']}")
    print("VERDICT:", verdict); print("CSV:", out_csv)


if __name__ == "__main__":
    main()
