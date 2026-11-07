#!/usr/bin/env python3
"""Read the BQ76952 probe results from the running STM32 over SWD (READ-ONLY) and write a CSV.

Reads the g_bms_dbg struct (see Inc/bms_probe.h) with STM32_Programmer_CLI in HOTPLUG mode:
no halt, no reset, no writes. The struct address is looked up in the ELF, so it stays
correct after a rebuild. Values are RAW register contents; unit scaling (stack/pack/LD/
current) is not applied because it has not been verified.

Usage: python3 -I tools/read_bms_csv.py [--out PATH] [--samples N] [--interval SECONDS]
"""
import argparse, csv, datetime, os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUNDLES = os.path.expanduser("~/Library/Application Support/stm32cube/bundles")
PROG = os.path.join(BUNDLES, "programmer/2.23.0/bin/STM32_Programmer_CLI")
NM = os.path.join(BUNDLES, "gnu-tools-for-stm32/14.3.1+st.2/bin/arm-none-eabi-nm")
ELF = os.path.join(ROOT, "build/Debug/BMS-MCU-V1.elf")
STLINK_SN = "37FF71064E573436D8421743"

CMDS = [0x00, 0x03, 0x05, 0x07, 0x12] + list(range(0x14, 0x34, 2)) + [0x34, 0x36, 0x38, 0x3A, 0x68]
NAMES = {0x00: "ControlStatus", 0x03: "SafetyStatusA", 0x05: "SafetyStatusB",
         0x07: "SafetyStatusC", 0x12: "BatteryStatus", 0x34: "StackVoltage",
         0x36: "PACKpin", 0x38: "LDpin", 0x3A: "CC2Current", 0x68: "InternalTemp"}
UNITS = {"Cell": "mV (per BQ datasheet)", "InternalTemp": "0.1 K (assumed, unverified)"}
STATUS = {0: "OK", 1: "BUS_BUSY", 2: "NACK_ADDR", 3: "NACK_DATA", 4: "STRETCH"}

# Struct layout of bms_dbg_t (bytes)
OFF_PASS, OFF_IDLE, OFF_PRIMARY, OFF_STATUS, OFF_RAW = 0x00, 0x04, 0x08, 0x1C, 0x84
STRUCT_SIZE = 0xEC


def crc8(data):
    c = 0
    for x in data:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def symbol_addr(name):
    out = subprocess.run([NM, ELF], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    sys.exit(f"symbol {name} not found in {ELF}")


def read_struct(addr):
    cmd = [PROG, "-c", "port=SWD", f"sn={STLINK_SN}", "mode=HOTPLUG", "freq=1800",
           "-r32", f"0x{addr:08X}", f"0x{STRUCT_SIZE:X}"]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout
    out = re.sub(r"\x1b\[[0-9;]*m", "", out)
    words = {}
    for m in re.finditer(r"^0x([0-9A-Fa-f]{8}) :((?: [0-9A-Fa-f]{8})+)", out, re.M):
        a = int(m.group(1), 16)
        for i, w in enumerate(m.group(2).split()):
            words[a + 4 * i] = int(w, 16)
    if len(words) < STRUCT_SIZE // 4:
        sys.exit("SWD read failed or incomplete:\n" + out[-600:])
    return bytes(b for a in sorted(words) for b in words[a].to_bytes(4, "little"))


def u32(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None)
    ap.add_argument("--samples", type=int, default=1)
    ap.add_argument("--interval", type=float, default=1.0)
    args = ap.parse_args()

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out = args.out or os.path.join(ROOT, "data", f"bms_readout_{stamp}.csv")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    addr = symbol_addr("g_bms_dbg")

    header = ["timestamp", "sample", "mcu_pass_count", "i2c_idle_scl_sda", "primary_probe",
              "cmd_hex", "register", "i2c_status", "raw_bytes_hex", "crc_ok",
              "value_unsigned", "value_signed", "value_hex", "unit_note"]
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for s in range(args.samples):
            buf = read_struct(addr)
            ts = datetime.datetime.now().isoformat(timespec="seconds")
            idle = u32(buf, OFF_IDLE)
            primary = STATUS.get(u32(buf, OFF_PRIMARY), str(u32(buf, OFF_PRIMARY)))
            for i, c in enumerate(CMDS):
                st = u32(buf, OFF_STATUS + 4 * i)
                r = list(buf[OFF_RAW + 4 * i: OFF_RAW + 4 * i + 4])
                # Replies carry a CRC-8 after each data byte (verified on this board).
                crc_ok = crc8([0x10, c, 0x11, r[0]]) == r[1] and crc8([r[2]]) == r[3]
                val = r[0] | (r[2] << 8)
                sval = val - 65536 if val >= 32768 else val
                name = NAMES.get(c) or f"Cell{(c - 0x14) // 2 + 1}"
                unit = next((v for k, v in UNITS.items() if name.startswith(k)), "raw (scaling unverified)")
                w.writerow([ts, s, u32(buf, OFF_PASS), f"SCL={idle & 1} SDA={(idle >> 1) & 1}",
                            primary, f"0x{c:02X}", name, STATUS.get(st, st),
                            " ".join(f"{x:02X}" for x in r), crc_ok, val, sval, f"0x{val:04X}", unit])
            if s + 1 < args.samples:
                time.sleep(args.interval)
    print(out)


if __name__ == "__main__":
    main()
