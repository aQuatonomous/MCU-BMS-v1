#!/usr/bin/env python3
"""Decode the BQ config read-out (data/*_bq_config.csv) and compare it with docs/Limits_defaults.md.

Writes docs/Limits_check.md. Decode rules come ONLY from (a) the owner's docs/Limits_defaults.md and
(b) the constants in bq76952-main/bq76952.cpp. Anything not covered by those is shown RAW and labelled.
Reading registers cannot prove a *range limit* (that would need out-of-range writes); it only shows
whether the CURRENT values sit inside the documented range and decode sensibly.
Usage: python3 -I tools/check_limits.py [config_csv]
"""
import csv, glob, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = sys.argv[1] if len(sys.argv) > 1 else sorted(glob.glob(os.path.join(ROOT, "data", "*_bq_config.csv")))[-1]
rows = {r["request_hex"].lower(): r for r in csv.DictReader(open(src))}
RSENSE_MOHM = 1.0                      # owner: 1 mOhm sense resistor
SCD_MV = [10, 20, 40, 60, 80, 100, 125, 150, 175, 200, 250, 300, 350, 400, 450, 500]  # library enum / doc


def b(addr, n=1):
    d = bytes(int(x, 16) for x in rows[f"0x{addr:04x}"]["data_hex_lsb_first"].split())
    return d[:n]


def u(addr, n=1): return int.from_bytes(b(addr, n), "little")
def s(addr, n=1): return int.from_bytes(b(addr, n), "little", signed=True)


out = []
def table(title, head, body, note=None):
    out.append(f"\n### {title}\n")
    if note: out.append(note + "\n")
    out.append("| " + " | ".join(head) + " |")
    out.append("| " + " | ".join("---" for _ in head) + " |")
    for r in body: out.append("| " + " | ".join(str(x) for x in r) + " |")


def inr(v, lo, hi): return "yes" if lo <= v <= hi else "**NO**"

# ---------------- thresholds vs the owner's doc ----------------
cov, cuv = u(0x9278), u(0x9275)
occ, ocd1, ocd2, scd = u(0x9280), u(0x9282), u(0x9284), u(0x9286)
t = []
t.append(["Cell overvoltage (COV)", "0x9278", f"0x{cov:02X} ({cov})", f"{cov*50.6/1000:.3f} V", "1.012 – 5.566 V (raw 20–110)", inr(cov, 20, 110)])
t.append(["Cell undervoltage (CUV)", "0x9275", f"0x{cuv:02X} ({cuv})", f"{cuv*50.6/1000:.3f} V", "1.012 – 4.048 V (raw 20–80); library allows up to raw 90 = 4.554 V", inr(cuv, 20, 80)])
t.append(["Overcurrent charge (OCC)", "0x9280", f"0x{occ:02X} ({occ})", f"{occ*2} mV = {occ*2/RSENSE_MOHM:.0f} A @1 mΩ", "4 – 124 mV (raw 2–62)", inr(occ, 2, 62)])
t.append(["Overcurrent discharge 1 (OCD1)", "0x9282", f"0x{ocd1:02X} ({ocd1})", f"{ocd1*2} mV = {ocd1*2/RSENSE_MOHM:.0f} A @1 mΩ", "4 – 200 mV (raw 2–100)", inr(ocd1, 2, 100)])
t.append(["Overcurrent discharge 2 (OCD2)", "0x9284", f"0x{ocd2:02X} ({ocd2})", f"{ocd2*2} mV = {ocd2*2/RSENSE_MOHM:.0f} A @1 mΩ", "4 – 200 mV (raw 2–100)", inr(ocd2, 2, 100)])
t.append(["Short circuit discharge (SCD)", "0x9286", f"0x{scd:02X} (index {scd})", f"{SCD_MV[scd]} mV = {SCD_MV[scd]/RSENSE_MOHM:.0f} A @1 mΩ" if scd < 16 else "index out of range", "10 – 500 mV, 16 steps (index 0–15)", inr(scd, 0, 15)])
for name, addr, lo, hi in [("Overtemp charge (OTC)", 0x929A, -40, 120), ("Overtemp discharge (OTD)", 0x929D, -40, 120),
                           ("Overtemp internal (OTINT)", 0x92A3, -40, 120), ("Undertemp charge (UTC)", 0x92A6, -40, 120),
                           ("Undertemp discharge (UTD)", 0x92A9, -40, 120), ("Undertemp internal (UTINT)", 0x92AC, -40, 120),
                           ("Overtemp FET (OTF)", 0x92A0, 0, 150)]:
    v = s(addr)
    t.append([name, f"0x{addr:04X}", f"0x{v & 0xFF:02X} ({v})", f"{v} °C (signed int8 assumed)", f"{lo} – {hi} °C", inr(v, lo, hi)])
table("Thresholds on the chip vs docs/Limits_defaults.md", ["Feature", "Address", "Raw", "Decoded", "Documented range", "In range?"], t,
      "Delays are shown raw in the CSV (their time units are not defined in the owner's doc). Within-range only shows the current value is legal; it does not prove the range limits themselves.")

# ---------------- reactions ----------------
def bits(v, names):
    return ", ".join(n for i, n in sorted(names.items(), reverse=True) if v >> i & 1) or "none"
A = {7: "SCD", 6: "OCD2", 5: "OCD1", 4: "OCC", 3: "COV", 2: "CUV"}
B = {7: "OTF", 6: "OTINT", 5: "OTD", 4: "OTC", 2: "UTINT", 1: "UTD", 0: "UTC"}
C = {7: "OCD3", 6: "SCDL", 5: "OCDL", 1: "HWDF"}
r = []
for name, addr, nm, src_ in [("Enabled Protections A", 0x9261, A, "library bit comments (layout assumed same as Safety Status A)"),
                             ("Enabled Protections B", 0x9262, B, "library"), ("Enabled Protections C", 0x9263, C, "library"),
                             ("DSG FET Protections A", 0x9269, A, "library"), ("DSG FET Protections B", 0x926A, B, "library"), ("DSG FET Protections C", 0x926B, C, "library"),
                             ("CHG FET Protections A", 0x9265, A, "library"), ("CHG FET Protections B", 0x9266, B, "library (bits partly undocumented)"),
                             ("CHG FET Protections C", 0x9267, C, "library (bits partly undocumented)"),
                             ("SF Alert Mask A", 0x926F, A, "library"), ("SF Alert Mask B", 0x9270, B, "library"), ("SF Alert Mask C", 0x9271, C, "library")]:
    v = u(addr); r.append([name, f"0x{addr:04X}", f"0x{v:02X}", bits(v, nm), src_])
table("Which protections run, what they do to the FETs, what reaches the alarm", ["Register", "Address", "Raw", "Bits set (named)", "Bit names from"], r,
      "'Enabled Protections' = which checks are active. 'DSG/CHG FET Protections' = which faults switch that FET off *when that check is active*. A fault only opens a FET if it is BOTH enabled and listed in that FET's protection register.")

# ---------------- others ----------------
cc, cap = struct.unpack("<f", b(0x91A8, 4))[0], struct.unpack("<f", b(0x91AC, 4))[0]
o = [["CC Gain (float32)", "0x91A8", f"{cc:.4f}", "owner's table: 7.4768"],
     ["Capacity Gain (float32)", "0x91AC", f"{cap:.3f}", "owner's table: 2230042.463; CC Gain × 298261.6178 = %.3f" % (cc * 298261.6178)],
     ["Board Offset (int16)", "0x91C8", str(s(0x91C8, 2)), "owner's table: 0"],
     ["CC Offset Samples (uint16)", "0x91C6", str(u(0x91C6, 2)), "owner's table: 64"],
     ["CC Deadband (uint8)", "0x922D", str(u(0x922D)), "owner's table: 9"],
     ["Shutdown Stack Voltage (uint16)", "0x9241", f"{u(0x9241,2)} (raw)", "units not verified"],
     ["Shutdown Cell Voltage (uint16)", "0x923F", f"{u(0x923F,2)} (raw)", "0 may mean disabled - not verified"],
     ["Shutdown Temperature (int8)", "0x9244", f"{s(0x9244)} (°C assumed)", ""],
     ["Auto Shutdown Time / FET Off Delay / Shutdown Cmd Delay", "0x9254/0x9252/0x9253", f"{u(0x9254)} / {u(0x9252)} / {u(0x9253)}", "units not verified"],
     ["Sleep Current (int16)", "0x9248", f"{s(0x9248,2)} (raw)", "units not verified"],
     ["DA Configuration", "0x9303", f"0x{u(0x9303):02X}", "library: bits 1-0 = USER_AMPS (1 = mA units); bit 2 not documented in the references"],
     ["Default Alarm Mask (uint16)", "0x926D", f"0x{u(0x926D,2):04X}", "bits per owner's docs/Status_bit_mapping.md: 15 SSBC, 14 SSA, 13 PF, 12 MSK_SFALERT, 11 MSK_PFALERT"],
     ["Protection Configuration (uint16)", "0x925F", f"0x{u(0x925F,2):04X}", "library documents only bits 9/10 (current-based recovery); not decoded"]]
table("Calibration / shutdown / other (raw unless a source defines it)", ["Item", "Address", "Value", "Note"], o)

open(os.path.join(ROOT, "docs", "Limits_check.md"), "w").write(
    "# Limits check (generated by tools/check_limits.py)\n\nSource data: `" + os.path.relpath(src, ROOT) +
    "`. Rsense = 1 mΩ (owner). Decode rules: owner's `docs/Limits_defaults.md` and constants in `bq76952-main/bq76952.cpp` only.\n"
    + "\n".join(out) + "\n")
print("\n".join(out))
