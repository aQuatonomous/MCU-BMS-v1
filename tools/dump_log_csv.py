#!/usr/bin/env python3
"""Dump the MCU's RAM load-test log (g_log / g_log_samples) over SWD (READ-ONLY) to a CSV.

One SWD connection reads the header, then exactly `count` samples. Values are RAW register
contents plus clearly labelled derived columns. Derived columns rest on assumptions that are
NOT verified here:
  * current_mA / current_A : 0x3A read as a signed value in mA (DA Configuration USER_AMPS = 01
                        = mA per the library; the logged values step by ~7.5 = CC Gain, i.e. the BQ
                        has ALREADY applied gain/offset - so no further equation is applied here).
                        Pass --cc-counts to instead treat 0x3A as raw counts and apply
                        (raw - offset/samples) * gain (the old, double-converting behaviour).
  * temp_C_assumed    : raw/10 - 273.15, ASSUMING 0.1 K units.
LD / PACK / stack stay raw (unsigned 16-bit); give --pack-scale/--ld-scale/--stack-scale to add
scaled columns once the unit is known.
Usage: python3 -I tools/dump_log_csv.py [--out PATH] [--gain 7.4768] [--offset 0] [--offset-samples 64]
"""
import argparse, csv, datetime, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.expanduser("~/Library/Application Support/stm32cube/bundles")
PROG = os.path.join(B, "programmer/2.23.0/bin/STM32_Programmer_CLI")
NM = os.path.join(B, "gnu-tools-for-stm32/14.3.1+st.2/bin/arm-none-eabi-nm")
ELF = os.path.join(ROOT, "build/Debug/BMS-MCU-V1.elf")
SN = "37FF71064E573436D8421743"
SAMPLE = 28


def sym(name):
    for line in subprocess.run([NM, "-S", ELF], capture_output=True, text=True, check=True).stdout.splitlines():
        p = line.split()
        if len(p) == 4 and p[3] == name:
            return int(p[0], 16), int(p[1], 16)
    sys.exit(f"symbol {name} not found")


def read(addr, size):
    cmd = [PROG, "-c", "port=SWD", f"sn={SN}", "mode=HOTPLUG", "freq=4000", "-r32", f"0x{addr:08X}", f"0x{size:X}"]
    out = re.sub(r"\x1b\[[0-9;]*m", "", subprocess.run(cmd, capture_output=True, text=True, timeout=300).stdout)
    words = {}
    for m in re.finditer(r"^0x([0-9A-Fa-f]{8}) :((?: [0-9A-Fa-f]{8})+)", out, re.M):
        a = int(m.group(1), 16)
        for i, w in enumerate(m.group(2).split()):
            words[a + 4 * i] = int(w, 16)
    if len(words) < (size + 3) // 4:
        sys.exit("SWD read incomplete:\n" + out[-500:])
    return b"".join(words[a].to_bytes(4, "little") for a in sorted(words))[:size]


def s16(v):
    return v - 65536 if v >= 32768 else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--cc-counts", action="store_true", help="treat 0x3A as raw ADC counts and apply the gain equation")
    ap.add_argument("--gain", type=float, default=7.4768)
    ap.add_argument("--offset", type=float, default=0.0)
    ap.add_argument("--offset-samples", type=float, default=64.0)
    ap.add_argument("--pack-scale", type=float)
    ap.add_argument("--ld-scale", type=float)
    ap.add_argument("--stack-scale", type=float)
    a = ap.parse_args()

    haddr, _ = sym("g_log")
    hdr = read(haddr, 24)
    running, count, cap, period, errs, full = (int.from_bytes(hdr[i:i + 4], "little") for i in range(0, 24, 4))
    print(f"log: running={running} count={count} capacity={cap} period_ms={period} read_errors={errs} full={full}", file=sys.stderr)
    if count == 0:
        sys.exit("no samples recorded")
    count = min(count, cap)
    saddr, _ = sym("g_log_samples")
    # The programmer CLI refuses single reads over 32 KB: read in chunks of whole samples.
    buf = b""
    per_chunk = 1000
    for first in range(0, count, per_chunk):
        n = min(per_chunk, count - first)
        buf += read(saddr + first * SAMPLE, n * SAMPLE)

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out = a.out or os.path.join(ROOT, "data", f"load_log_{stamp}.csv")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    cols = ["t_ms", "cell1_mV", "cell2_mV", "cell3_mV", "cell16_mV", "stack_raw", "pack_raw", "ld_raw",
            "cc2_raw_u16", "cc2_raw_signed", "current_mA", "current_A", "alarm_raw_hex", "battery_status_hex",
            "fet_status_hex", "CHG", "PCHG", "DSG", "PDSG", "DCHG_pin", "DDSG_pin", "XCHG", "XDSG", "SLEEP",
            "SDM", "temp_raw", "temp_C_assumed", "read_error", "slow_fresh"]
    if a.stack_scale: cols.append("stack_scaled")
    if a.pack_scale: cols.append("pack_scaled")
    if a.ld_scale: cols.append("ld_scaled")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for i in range(count):
            o = i * SAMPLE
            t = int.from_bytes(buf[o:o + 4], "little")
            u = [int.from_bytes(buf[o + 4 + 2 * k: o + 6 + 2 * k], "little") for k in range(11)]
            fet, flags = buf[o + 26], buf[o + 27]
            c1, c2, c3, c16, stack, pack, ld, cc2, alarm, batt, temp = u
            if cc2 == 0xFFFF: cur_mA = ""
            elif a.cc_counts: cur_mA = (s16(cc2) - a.offset / a.offset_samples) * a.gain
            else: cur_mA = s16(cc2)
            row = [t, c1, c2, c3, c16, stack, pack, ld, cc2, s16(cc2), round(cur_mA, 3) if cur_mA != "" else "", round(cur_mA / 1000.0, 5) if cur_mA != "" else "",
                   f"0x{alarm:04X}", f"0x{batt:04X}", f"0x{fet:02X}",
                   fet & 1, (fet >> 1) & 1, (fet >> 2) & 1, (fet >> 3) & 1, (fet >> 4) & 1, (fet >> 5) & 1,
                   (alarm >> 6) & 1, (alarm >> 5) & 1, (batt >> 15) & 1, (batt >> 13) & 1,
                   temp, round(temp / 10 - 273.15, 2) if temp != 0xFFFF else "", flags & 1, (flags >> 1) & 1]
            if a.stack_scale: row.append(round(stack * a.stack_scale, 4))
            if a.pack_scale: row.append(round(pack * a.pack_scale, 4))
            if a.ld_scale: row.append(round(ld * a.ld_scale, 4))
            w.writerow(row)
    print(out)


if __name__ == "__main__":
    main()
