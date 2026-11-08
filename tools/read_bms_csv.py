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

NAMES = {0x00: "ControlStatus", 0x02: "SafetyAlertA", 0x03: "SafetyStatusA", 0x04: "SafetyAlertB",
         0x05: "SafetyStatusB", 0x06: "SafetyAlertC", 0x07: "SafetyStatusC", 0x12: "BatteryStatus",
         0x34: "StackVoltage", 0x36: "PACKpin", 0x38: "LDpin", 0x3A: "CC2Current",
         0x62: "AlarmStatus", 0x64: "AlarmRawStatus", 0x66: "AlarmEnable",
         0x68: "InternalTemp", 0x7F: "FETStatus"}
UNITS = {"Cell": "mV (per BQ datasheet)", "InternalTemp": "0.1 K (assumed, unverified)",
         "CC2Current": "mA, already gain-applied (DA Config USER_AMPS=01 per library; ~7.5 step = CC Gain)"}
# Names come from TI's STM32_I2C/BQ769x2Header.h ("#define Name 0xADDR //Group:Sub:Name").
def load_dm_names():
    names = {}
    hdr = os.path.join(ROOT, "STM32_I2C", "BQ769x2Header.h")
    for line in open(hdr, errors="replace"):
        m = re.match(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)\s*(?://\s*(.*))?", line)
        if m:
            addr = int(m.group(2), 16)
            if addr >= 0x9000 or addr in (0x0001, 0x0057):
                names.setdefault(addr, (m.group(3) or m.group(1)).strip())
    # corrections / additions that are NOT in TI's header (sources noted in Src/bq_dm.c)
    names.update({0x91C6: "Calibration:Current Offset:CC Offset Samples (owner's table)",
                  0x922D: "CC Deadband (owner's table)",
                  0x929B: "Protections:OTC:Delay (address per library; TI header lists 0x920B)",
                  0x0001: "DEVICE_NUMBER", 0x0057: "MANUFACTURING_STATUS"})
    return names


DM_NAMES = load_dm_names()
# bq_dm_dbg_t: pass_count, n, then 16-byte records
# {u16 cmd, u8 status, u8 len, u8 chk_ok, u8 crc_ok, u8 chk_ok_echo, u8 chk_byte, u8 data[32], u16 echo, u16 pad}
DM_HDR, DM_REC = 8, 44
STATUS = {0: "OK", 1: "BUS_BUSY", 2: "NACK_ADDR", 3: "NACK_DATA", 4: "STRETCH"}

# Struct layout of bms_dbg_t (see Inc/bms_probe.h): 7 header words, then one 8-byte
# bms_reg_t {cmd, len, status, pad, raw[4]} per register. The record count comes from the
# symbol size in the ELF, so adding registers in firmware needs no change here.
OFF_PASS, OFF_IDLE, OFF_PRIMARY, OFF_REGS, REG_SIZE = 0x00, 0x04, 0x08, 0x1C, 8


def crc8(data):
    c = 0
    for x in data:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def symbol_addr_size(name):
    out = subprocess.run([NM, "-S", ELF], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] == name:
            return int(parts[0], 16), int(parts[1], 16)
    sys.exit(f"symbol {name} not found in {ELF}")


def read_struct(addr, size):
    cmd = [PROG, "-c", "port=SWD", f"sn={STLINK_SN}", "mode=HOTPLUG", "freq=1800",
           "-r32", f"0x{addr:08X}", f"0x{size:X}"]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout
    out = re.sub(r"\x1b\[[0-9;]*m", "", out)
    words = {}
    for m in re.finditer(r"^0x([0-9A-Fa-f]{8}) :((?: [0-9A-Fa-f]{8})+)", out, re.M):
        a = int(m.group(1), 16)
        for i, w in enumerate(m.group(2).split()):
            words[a + 4 * i] = int(w, 16)
    if len(words) < size // 4:
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
    addr, size = symbol_addr_size("g_bms_dbg")
    nregs = (size - OFF_REGS) // REG_SIZE

    header = ["timestamp", "sample", "mcu_pass_count", "i2c_idle_scl_sda", "primary_probe",
              "cmd_hex", "register", "i2c_status", "raw_bytes_hex", "crc_ok",
              "value_unsigned", "value_signed", "value_hex", "unit_note"]
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for s in range(args.samples):
            buf = read_struct(addr, size)
            ts = datetime.datetime.now().isoformat(timespec="seconds")
            idle = u32(buf, OFF_IDLE)
            primary = STATUS.get(u32(buf, OFF_PRIMARY), str(u32(buf, OFF_PRIMARY)))
            for i in range(nregs):
                o = OFF_REGS + REG_SIZE * i
                c, ln, st = buf[o], buf[o + 1], buf[o + 2]
                r = list(buf[o + 4: o + 8])
                # Replies carry a CRC-8 after each data byte (verified on this board).
                crc_ok = crc8([0x10, c, 0x11, r[0]]) == r[1]
                val = r[0]
                if ln == 2:
                    crc_ok = crc_ok and crc8([r[2]]) == r[3]
                    val = r[0] | (r[2] << 8)
                    sval = val - 65536 if val >= 32768 else val
                else:
                    sval = val - 256 if val >= 128 else val
                name = NAMES.get(c) or f"Cell{(c - 0x14) // 2 + 1}"
                unit = next((v for k, v in UNITS.items() if name.startswith(k)), "raw (scaling unverified)")
                w.writerow([ts, s, u32(buf, OFF_PASS), f"SCL={idle & 1} SDA={(idle >> 1) & 1}",
                            primary, f"0x{c:02X}", name, STATUS.get(st, st),
                            " ".join(f"{x:02X}" for x in r[:2 * ln]), crc_ok, val, sval,
                            f"0x{val:0{2 * ln}X}", unit])
            if s + 1 < args.samples:
                time.sleep(args.interval)
    print(out)

    # ---- BQ configuration read-out (data memory), second CSV -------------------------
    daddr, dsize = symbol_addr_size("g_bq_dm")
    dbuf = read_struct(daddr, dsize)
    dn = (dsize - DM_HDR) // DM_REC
    dout = out.replace("bms_readout_", "bq_config_") if "bms_readout_" in out else out[:-4] + "_bq_config.csv"
    assert dout != out, "config CSV would overwrite the BMS CSV"
    with open(dout, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["timestamp", "mcu_dm_pass_count", "request_hex", "name", "i2c_status",
                    "reported_len", "checksum_ok_vs_request", "checksum_ok_vs_echo", "crc_ok",
                    "echo_3E_3F", "checksum_byte", "first_byte", "data_hex_lsb_first"])
        ts = datetime.datetime.now().isoformat(timespec="seconds")
        for i in range(dn):
            o = DM_HDR + DM_REC * i
            cmd = int.from_bytes(dbuf[o:o + 2], "little")
            st, ln, chk, crc, chk_e, chkb = dbuf[o + 2], dbuf[o + 3], dbuf[o + 4], dbuf[o + 5], dbuf[o + 6], dbuf[o + 7]
            data = dbuf[o + 8: o + 40][:min(ln, 32)]
            echo = int.from_bytes(dbuf[o + 40:o + 42], "little")
            w.writerow([ts, u32(dbuf, 0), f"0x{cmd:04X}", DM_NAMES.get(cmd, "?"), STATUS.get(st, st), ln,
                        bool(chk), bool(chk_e), bool(crc), f"0x{echo:04X}", f"0x{chkb:02X}",
                        f"0x{data[0]:02X}" if data else "",
                        " ".join(f"{x:02X}" for x in data)])
    print(dout)


if __name__ == "__main__":
    main()
