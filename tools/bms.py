#!/usr/bin/env python3
"""BMS-MCU-V1 command-line helper: build, flash, check, configure and run studies.

  python3 tools/bms.py status                    check every link (ST-Link, MCU, I2C, BQ, USB) and the BQ configuration
  python3 tools/bms.py build | flash             compile / compile + program the MCU (verified) + reset it
  python3 tools/bms.py config show|gen|apply     show / regenerate / regenerate+flash the start-up settings (config/*.csv)
  python3 tools/bms.py autoconfig pause|resume|now
  python3 tools/bms.py log start|stop|dump       RAM logger on the MCU (20 Hz default) and its CSV dump
  python3 tools/bms.py arm --yes                 set FET_EN and leave ALL FETs OFF (load must be DISCONNECTED while this runs)
  python3 tools/bms.py fets-on --yes | fets-off | sleep-disable | bq-reset --yes
  python3 tools/bms.py loadtest [options]        guarded load test (see tools/run_load_test.py --help)
  python3 tools/bms.py sweep-scd [options]       SCD-delay sweep   (see tools/sweep_scd_delay.py --help)
Full instructions: docs/User-Guide.md
"""
import argparse, csv, glob, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import dump_log_csv as D                      # PROG, NM, SN, sym(), read()

BUNDLES = os.path.expanduser("~/Library/Application Support/stm32cube/bundles")
ELF = os.path.join(ROOT, "build", "Debug", "BMS-MCU-V1.elf")
CMD_MAGIC, FETON, FETEN, LOG_START, LOG_STOP, AC_NOW = 0xFE7E0100, 0xFE7E0096, 0xFE7E0022, 0xFE7E1001, 0xFE7E1002, 0xFE7E0500
AC_STATES = {0: "NOT_RUN", 1: "OK_MATCH (all settings already correct)", 2: "APPLIED (settings repaired + verified)", 3: "VERIFY_FAIL", 4: "NO_BQ (BQ not answering)",
             5: "DEFERRED - a FET is on (left alone on purpose)", 6: "PAUSED", 7: "DEFERRED - logger running", 8: "WRITE_FAIL", 9: "CFGUPDATE_FAIL", 10: "READ_FAIL"}


def tool_dir(group, sub):
    c = sorted(glob.glob(os.path.join(BUNDLES, group, "*", *sub)))
    return os.path.dirname(c[-1]) if c else None


def env_with_tools():
    e = os.environ.copy()
    dirs = [tool_dir("gnu-tools-for-stm32", ["bin", "arm-none-eabi-gcc"]), tool_dir("ninja", ["bin", "ninja"]),
            tool_dir("cmake", ["CMake.app", "Contents", "bin", "cmake"])]
    e["PATH"] = os.pathsep.join([d for d in dirs if d] + [e.get("PATH", "")])
    return e


def run(cmd, **kw): return subprocess.run(cmd, text=True, **kw)
def clean(t): return re.sub(r"\x1b\[[0-9;]*m", "", t)


def swd(*args, timeout=90):
    r = run([D.PROG, "-c", "port=SWD", f"sn={D.SN}", "mode=HOTPLUG", "freq=1800", *args], capture_output=True, timeout=timeout)
    return clean(r.stdout)


def w32(name_or_addr, value):
    a = D.sym(name_or_addr)[0] if isinstance(name_or_addr, str) else name_or_addr
    swd("-w32", f"0x{a:08X}", f"0x{value:08X}")       # the CLI prints a harmless 'Failed to download data' on peripheral/RAM writes; read back to verify


def words(name, nwords):
    a = D.sym(name)[0]; b = D.read(a, 4 * nwords)
    return [int.from_bytes(b[i:i + 4], "little") for i in range(0, 4 * nwords, 4)]


def need_yes(args, why):
    if not args.yes:
        sys.exit(f"REFUSED: {why}\nRe-run with --yes if you are sure.")


# ------------------------------------------------------------------------------------------------ commands
def cmd_build(_a):
    e = env_with_tools()
    for step in (["cmake", "--preset", "Debug"], ["cmake", "--build", "--preset", "Debug"]):
        if run(step, cwd=ROOT, env=e).returncode: sys.exit("BUILD FAILED")
    print("build OK:", os.path.relpath(ELF, ROOT))


def cmd_flash(a):
    if not a.no_build: cmd_build(a)
    r = run([D.PROG, "-c", "port=SWD", f"sn={D.SN}", "freq=1800", "-d", ELF, "-v", "-rst"], capture_output=True)
    out = clean(r.stdout)
    print("\n".join(l for l in out.splitlines() if re.search(r"Erasing internal|verified|rror|Reset", l)))
    if "verified successfully" not in out: sys.exit("FLASH/VERIFY FAILED (see docs/User-Guide.md - Troubleshooting)")
    print("flashed + verified + MCU reset. The BQ keeps running; the MCU re-checks its settings within ~3 s.")


def cmd_config(a):
    gen = os.path.join(HERE, "gen_config.py")
    if a.what == "show":
        for f in ("bms_settings.csv", "bms_options.csv"):
            print("==", f); print(open(os.path.join(ROOT, "config", f)).read())
    elif a.what == "gen": sys.exit(run([sys.executable, "-I", gen]).returncode)
    else:
        if run([sys.executable, "-I", gen]).returncode: sys.exit("config generation failed - nothing flashed")
        cmd_flash(argparse.Namespace(no_build=False))


def cmd_autoconfig(a):
    if a.what == "pause": w32("g_autocfg_pause", 1); print("auto-configuration PAUSED (cleared by an MCU reset). Run 'autoconfig resume' when finished.")
    elif a.what == "resume": w32("g_autocfg_pause", 0); print("auto-configuration resumed")
    else: w32("g_autocfg_req", AC_NOW); print("check requested; see 'status' in ~10 s")


def cmd_log(a):
    if a.what == "start":
        w32("g_log_period_ms", a.period); w32("g_log_slow_div", a.slow_div); w32("g_log_req", LOG_START)
        print(f"logger started ({a.period} ms period, slow channel every {a.slow_div} samples). ~2048 samples, then it stops by itself.")
    elif a.what == "stop": w32("g_log_req", LOG_STOP); print("logger stopped")
    else:
        sys.exit(run([sys.executable, "-I", os.path.join(HERE, "dump_log_csv.py")] + (["--out", a.out] if a.out else [])).returncode)


def mailbox(idx, label):
    w32("g_bq_cmd_req", CMD_MAGIC | idx); time.sleep(3)
    res = words("g_bq_cmd_res", 4)
    print(f"{label}: done={res[0]} sent={res[1]} reason={res[2]} cmd=0x{res[3]:04X}", "(OK)" if res[:3] == [1, 1, 0] else "(NOT CONFIRMED)")


def cmd_fets(a):
    if a.cmd == "sleep-disable": mailbox(0, "SLEEP_DISABLE")
    elif a.cmd == "fets-off": mailbox(1, "ALL_FETS_OFF")
    elif a.cmd == "bq-reset":
        need_yes(a, "BQ reset wipes ALL BQ RAM settings (the MCU will re-apply config/bms_settings.csv within ~5 s) and clears FET_EN.")
        mailbox(2, "BQ software reset")
    elif a.cmd == "fets-on":
        need_yes(a, "This turns the CHG/PDSG/DSG FETs ON and energises whatever is connected to the load side.")
        w32("g_bq_fets_on_req", FETON); time.sleep(3); h = words("g_bq_fets_on_res", 5)
        print(f"ALL_FETS_ON: done={h[0]} sent={h[1]} reason={h[2]} (0 sent, 1 FET_EN not set -> run 'arm' first) Manufacturing Status before=0x{h[3]:04X} FET status after=0x{h[4]:02X}")


def cmd_arm(a):
    need_yes(a, "FET_ENABLE makes the BQ switch its FETs ON immediately (pre-discharge, then DSG/CHG). DISCONNECT THE LOAD first. This tool turns them OFF again about 4 s later.")
    w32("g_bq_fet_enable_req", FETEN); time.sleep(4); e = words("g_bq_fet_enable_res", 5)
    print(f"FET_ENABLE: done={e[0]} sent={e[1]} reason={e[2]} (0 sent, 1 already enabled) Manufacturing Status 0x{e[3]:04X} -> 0x{e[4]:04X}")
    mailbox(1, "ALL_FETS_OFF"); print("FET_EN is set and all FETs are OFF. 'status' will confirm.")


def cmd_passthrough(script, rest):
    sys.exit(run([sys.executable, "-I", os.path.join(HERE, script)] + rest).returncode)


def cmd_status(a):
    ok_all = True
    def line(tag, ok, text):
        nonlocal ok_all; ok_all &= bool(ok) if ok is not None else True
        print(f"  [{'OK' if ok else ('..' if ok is None else 'FAIL')}] {tag:<22} {text}")
    print("== Links ==")
    probes = clean(run([D.PROG, "-l", "st"], capture_output=True).stdout)
    sn = re.search(r"ST-LINK SN\s*:\s*(\S+)", probes); fw = re.search(r"ST-LINK FW\s*:\s*(\S+)", probes)
    line("ST-Link", bool(sn), f"SN {sn.group(1)} FW {fw.group(1)}" if sn else "no ST-Link found (USB cable / driver?)")
    if not sn: print("\nCannot continue without the ST-Link."); sys.exit(1)
    if sn.group(1) != D.SN: line("ST-Link serial", False, f"expected {D.SN}; set BMS_STLINK_SN to use this probe")
    txt = swd("-r32", "0xE0042000", "4", "-r32", f"0x{D.sym('g_clock_status')[0]:08X}", "4", "-r32", "0x40023808", "4", "-r32", "0x40020800", "4", "-r32", "0x40020814", "4", "-r32", "0xE000EDF0", "4")
    v = re.search(r"Voltage\s*:\s*([\d.]+)V", txt); dev = re.search(r"Device ID\s*:\s*(0x\w+)", txt)
    mem = {int(m.group(1), 16): int(m.group(2), 16) for m in re.finditer(r"^0x([0-9A-Fa-f]{8}) : ([0-9A-Fa-f]{8})", txt, re.M)}
    if not dev or 0xE0042000 not in mem: line("MCU over SWD", False, "ST-Link found but the MCU does not answer (power? SWD wires? BOOT0?)"); sys.exit(1)
    line("MCU over SWD", dev.group(1) == "0x411", f"target {v.group(1) if v else '?'} V, device {dev.group(1)} (0x411 = STM32F2xx)")
    cs = mem[D.sym("g_clock_status")[0]]; sws = (mem[0x40023808] >> 2) & 3; dh = mem[0xE000EDF0]
    line("MCU clock", cs == 0 and sws == 2, f"g_clock_status={cs} (0 = HSE+PLL ok), SYSCLK source {'PLL' if sws == 2 else 'NOT PLL'}")
    line("MCU core", not (dh & 0x20000) and not (dh & 0x80000), "running" if not (dh & 0x20000 or dh & 0x80000) else "HALTED or LOCKED UP")
    line("RST_SHUT (PC0)", (mem[0x40020800] & 3) == 1 and not (mem[0x40020814] & 1), "driven low by the MCU" if (mem[0x40020800] & 3) == 1 and not (mem[0x40020814] & 1) else "NOT driven low")
    outdir = os.path.join(ROOT, "data", "status"); os.makedirs(outdir, exist_ok=True); out = os.path.join(outdir, "latest.csv")
    run([sys.executable, "-I", os.path.join(HERE, "read_bms_csv.py"), "--samples", "1", "--out", out], capture_output=True)
    rows = list(csv.DictReader(open(out))); r = {x["register"]: x for x in rows}
    c = list(csv.DictReader(open(out[:-4] + "_bq_config.csv"))); cd = {x["request_hex"]: x for x in c}
    line("I2C bus / BQ", rows[0]["primary_probe"] == "OK" and rows[0]["i2c_idle_scl_sda"] == "SCL=1 SDA=1", f"lines {rows[0]['i2c_idle_scl_sda']}, BQ at 0x08: {rows[0]['primary_probe']}")
    dn = cd.get("0x0001"); dnv = int.from_bytes(bytes(int(y, 16) for y in dn["data_hex_lsb_first"].split()[:2]), "little") if dn and dn["data_hex_lsb_first"] else None
    line("BQ identity", dnv == 0x7695, f"DEVICE_NUMBER 0x{dnv:04X}" if dnv is not None else "could not read DEVICE_NUMBER")
    usb = clean(run(["system_profiler", "SPUSBDataType"], capture_output=True).stdout)
    line("BMS USB (1209:0001)", True if "BMS MCU" in usb or "0x1209" in usb else None, "enumerated on this computer" if ("BMS MCU" in usb or "0x1209" in usb) else "not seen (USB-C cable not connected / not enumerating - not needed for SWD work)")
    print("\n== BQ state ==")
    if rows[0]["primary_probe"] != "OK": print("  BQ not answering - values below are stale. See Troubleshooting."); sys.exit(1)
    bs = int(r["BatteryStatus"]["value_hex"], 16); fs = int(r["FETStatus"]["value_hex"], 16)
    saf = [r[k]["value_hex"] for k in ("SafetyStatusA", "SafetyStatusB", "SafetyStatusC")]
    mfg = int(cd["0x0057"]["data_hex_lsb_first"].split()[0], 16) if cd["0x0057"]["data_hex_lsb_first"] else None
    line("FETs", True, f"CHG={fs & 1} PCHG={(fs >> 1) & 1} DSG={(fs >> 2) & 1} PDSG={(fs >> 3) & 1}  ({'all OFF' if not fs & 0xF else 'SOME ARE ON'})  FET_EN={'set' if mfg is not None and mfg & 0x10 else 'clear'}")
    line("Safety / PF flags", all(int(x, 16) == 0 for x in saf) and not (bs >> 12) & 1, f"Safety A/B/C {saf}, PF bit {(bs >> 12) & 1}, FUSE bit {(bs >> 10) & 1}")
    line("Mode", None, f"SLEEP={(bs >> 15) & 1} shutdown-pending={(bs >> 13) & 1} security={['uninit', 'FULLACCESS', 'UNSEALED', 'SEALED'][(bs >> 8) & 3]} POR={(bs >> 3) & 1}")
    print(f"  cells mV (cells 1,2,3,16): {[r[k]['value_unsigned'] for k in ('Cell1', 'Cell2', 'Cell3', 'Cell16')]} | unused 4-15: {sorted(set(int(r['Cell%d' % n]['value_signed']) for n in range(4, 16)))}"
          f" | current {r['CC2Current']['value_signed']} mA | stack/pack/LD raw {r['StackVoltage']['value_unsigned']}/{r['PACKpin']['value_unsigned']}/{r['LDpin']['value_unsigned']}")
    print("\n== Start-up configuration (config/bms_settings.csv, compiled into the MCU) ==")
    h = words("g_autocfg", 9 + 2 * 32)
    line("auto-config", h[1] in (1, 2), f"{AC_STATES.get(h[0], h[0])}; last result {AC_STATES.get(h[1], h[1])}; checks={h[2]} repairs={h[3]} errors={h[5]}")
    names = {}
    for x in csv.DictReader(open(os.path.join(ROOT, "config", "bms_settings.csv"))):
        try: names[int(x["address"], 0)] = x["name"]
        except Exception: pass
    for i in range(h[6]):
        d0, d1 = h[9 + 2 * i], h[9 + 2 * i + 1]
        addr, nb, fl, want, live = d0 & 0xFFFF, (d0 >> 16) & 0xFF, (d0 >> 24) & 0xFF, d1 & 0xFFFF, (d1 >> 16) & 0xFFFF
        print(f"      0x{addr:04X} {names.get(addr, '?'):<24} want 0x{want:0{2 * nb}X}  live {('0x%0*X' % (2 * nb, live)) if fl & 1 else 'unread':<8} {'MATCH' if fl & 2 else ('DIFFERS' if fl & 1 else '-')}")
    print("\nOVERALL:", "all checks passed" if ok_all else "something needs attention (see FAIL lines)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="c", required=True)
    sp.add_parser("status"); sp.add_parser("build")
    p = sp.add_parser("flash"); p.add_argument("--no-build", action="store_true")
    p = sp.add_parser("config"); p.add_argument("what", choices=["show", "gen", "apply"])
    p = sp.add_parser("autoconfig"); p.add_argument("what", choices=["pause", "resume", "now"])
    p = sp.add_parser("log"); p.add_argument("what", choices=["start", "stop", "dump"]); p.add_argument("--period", type=int, default=50); p.add_argument("--slow-div", type=int, default=5); p.add_argument("--out")
    p = sp.add_parser("arm"); p.add_argument("--yes", action="store_true")
    for n in ("fets-on", "fets-off", "sleep-disable", "bq-reset"):
        p = sp.add_parser(n); p.add_argument("--yes", action="store_true")
    for n in ("loadtest", "sweep-scd"): sp.add_parser(n, add_help=False)
    a, rest = ap.parse_known_args()
    {"status": cmd_status, "build": cmd_build, "flash": cmd_flash, "config": cmd_config, "autoconfig": cmd_autoconfig, "log": cmd_log, "arm": cmd_arm}.get(a.c, lambda x: None)(a) if a.c in ("status", "build", "flash", "config", "autoconfig", "log", "arm") else None
    if a.c in ("fets-on", "fets-off", "sleep-disable", "bq-reset"): a.cmd = a.c; cmd_fets(a)
    if a.c == "loadtest": cmd_passthrough("run_load_test.py", rest)
    if a.c == "sweep-scd": cmd_passthrough("sweep_scd_delay.py", rest)


if __name__ == "__main__":
    main()
