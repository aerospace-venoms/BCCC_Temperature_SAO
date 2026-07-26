#!/usr/bin/env python3
"""
Batch flash + test station for the BCCC Temperature SAO (RP2350).

Watches for RP2350 boards appearing in BOOTSEL mode, flashes each one with the
release firmware, then reopens it as a USB serial device and verifies that the
firmware actually runs. Several boards are handled at once, each in its own
worker thread, so you can keep plugging cables in.

Boards are tracked by their USB serial number (the chip's unique ID), which is
reported identically by the BOOTSEL bootloader and by the running firmware.
That is what makes parallel operation safe: a board's post-flash serial port is
matched back to the exact board that was flashed, no matter what order things
enumerate in.

A DS18B20 does not need to be fitted. A board with no sensor reports
PASS (no sensor) rather than failing, so this works both before and after the
sensors are attached.

Usage:
    tools/flash_and_test.py                          # auto-picks firmware/*.uf2
    tools/flash_and_test.py -f firmware/x.uf2        # explicit image
    tools/flash_and_test.py --expect-version 1.0.1   # fail on version mismatch
    tools/flash_and_test.py --require-sensor         # treat missing DS18B20 as a failure
    tools/flash_and_test.py --log run.csv            # append per-board results

Ctrl-C to stop; a summary is printed on exit.
"""

from __future__ import annotations

import argparse
import csv
import glob
import os
import re
import select
import subprocess
import sys
import termios
import threading
import time
import tty
from dataclasses import dataclass, field
from datetime import datetime

USB_DEVICES = "/sys/bus/usb/devices"
VID_RASPBERRYPI = "2e8a"
PID_RP2350_BOOTSEL = "000f"

# Expected serial output from the firmware.
RE_BANNER = re.compile(r"firmware v([0-9A-Za-z.+-]+)\s*\(HW rev (\d+)\)")
RE_SENSOR_OK = re.compile(r"Sensor:\s*DS18B20 on GPIO")
RE_SENSOR_MISSING = re.compile(r"Sensor:\s*DS18B20 not found")
RE_READING_EXT = re.compile(r"DS18B20:\s*(-?\d+\.?\d*)\s*F")
RE_READING_INT = re.compile(r"Internal:\s*(-?\d+\.?\d*)\s*F")

# Plausibility bands (degrees F).
AMBIENT_MIN, AMBIENT_MAX = 32.0, 120.0
DIE_MIN, DIE_MAX = 40.0, 190.0

print_lock = threading.Lock()
results: list["Result"] = []
results_lock = threading.Lock()
stop_event = threading.Event()

# Chip serials currently being flashed/verified, so one board is never picked
# up by two workers while it re-enumerates.
active: set[str] = set()
active_lock = threading.Lock()

# picotool has to open candidate USB devices to read their serial numbers, so
# two instances running at once make each other's target look inaccessible.
# Flashing is therefore serialised; it is quick, and the slow part (listening
# to each board's serial output) still happens fully in parallel.
picotool_lock = threading.Lock()


def log(msg: str) -> None:
    with print_lock:
        print(msg, flush=True)


def read_sysfs(path: str) -> str | None:
    try:
        with open(path) as fh:
            return fh.read().strip()
    except OSError:
        return None


@dataclass
class Board:
    """An RP2350 sitting in BOOTSEL mode."""
    devpath: str        # e.g. "1-2.3" - the physical USB port path
    busnum: int
    devnum: int
    serial: str | None

    @property
    def instance_key(self) -> str:
        # devnum changes on every re-plug, so this identifies one enumeration.
        return f"{self.devpath}:{self.devnum}"

    @property
    def label(self) -> str:
        return f"port {self.devpath}" + (f" [{self.serial}]" if self.serial else "")


@dataclass
class Result:
    label: str
    serial: str | None
    status: str                     # PASS | PASS_NO_SENSOR | FAIL
    detail: str = ""
    version: str | None = None
    hw_rev: str | None = None
    sensor: str = "unknown"         # present | absent | unknown
    temperature: float | None = None
    when: str = field(default_factory=lambda: datetime.now().isoformat(timespec="seconds"))

    @property
    def ok(self) -> bool:
        return self.status.startswith("PASS")


def find_bootsel_boards() -> list[Board]:
    """Scan sysfs for RP2350 devices currently in BOOTSEL mode."""
    boards = []
    for entry in glob.glob(os.path.join(USB_DEVICES, "*")):
        vid = read_sysfs(os.path.join(entry, "idVendor"))
        pid = read_sysfs(os.path.join(entry, "idProduct"))
        if vid != VID_RASPBERRYPI or pid != PID_RP2350_BOOTSEL:
            continue
        busnum = read_sysfs(os.path.join(entry, "busnum"))
        devnum = read_sysfs(os.path.join(entry, "devnum"))
        if not busnum or not devnum:
            continue
        # A device still enumerating can briefly report devnum 0, which is not
        # a usable USB address. Skip it and pick it up on the next poll.
        if int(devnum) < 1:
            continue
        boards.append(Board(
            devpath=os.path.basename(entry),
            busnum=int(busnum),
            devnum=int(devnum),
            serial=read_sysfs(os.path.join(entry, "serial")),
        ))
    return boards


def find_serial_port(serial: str | None, devpath: str | None) -> str | None:
    """
    Locate the /dev/ttyACM* belonging to a specific board.

    Prefers matching the USB serial number (robust even if the board lands on a
    different port); falls back to the USB port path when the device does not
    expose a serial string.
    """
    for tty_dev in glob.glob("/sys/class/tty/ttyACM*"):
        name = os.path.basename(tty_dev)
        # .../usbN/A-B/A-B.C/A-B.C:1.0 -> the USB device is the interface's parent
        iface = os.path.realpath(os.path.join(tty_dev, "device"))
        usbdev = os.path.dirname(iface)
        if read_sysfs(os.path.join(usbdev, "idVendor")) != VID_RASPBERRYPI:
            continue
        dev_serial = read_sysfs(os.path.join(usbdev, "serial"))
        if serial and dev_serial:
            if dev_serial == serial:
                return f"/dev/{name}"
        elif devpath and os.path.basename(usbdev) == devpath:
            return f"/dev/{name}"
    return None


def flash(board: Board, uf2: str, timeout: int) -> tuple[bool, str]:
    """Load the UF2 onto one specific board and reboot it into the application."""
    # Prefer the chip's serial number: it is stable, whereas the USB device
    # address changes whenever the board re-enumerates, which can happen
    # between the sysfs scan and this call.
    cmd = ["picotool", "load", "-x", uf2]
    if board.serial:
        cmd += ["--ser", board.serial]
    else:
        cmd += ["--bus", str(board.busnum), "--address", str(board.devnum)]
    last_err = ""
    for attempt in (1, 2):
        try:
            with picotool_lock:
                proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return False, f"picotool timed out after {timeout}s"
        except FileNotFoundError:
            return False, "picotool not found on PATH"
        if proc.returncode == 0:
            return True, ""
        last_err = (proc.stderr or proc.stdout or "").strip().replace("\n", " ")
        # A board can be mid-re-enumeration; one retry clears that up.
        if attempt == 1:
            time.sleep(1.0)

    # Only suggest udev rules for a genuine permission problem. picotool says
    # "no accessible device" when it simply cannot match, which is different.
    if "permission" in last_err.lower() or "libusb_error_access" in last_err.lower():
        last_err += "  (install picotool udev rules, or run with sudo)"
    return False, last_err or f"picotool exited {proc.returncode}"


def open_port(port: str, timeout: float = 5.0) -> int:
    """
    Open a freshly-created tty, retrying past the udev race.

    The device node appears slightly before udev applies its group ownership,
    so an immediate open can fail with EACCES even though the user is in the
    dialout group. Retry briefly rather than reporting a bogus failure.
    """
    deadline = time.monotonic() + timeout
    last: OSError | None = None
    while time.monotonic() < deadline:
        try:
            return os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
        except PermissionError as exc:
            last = exc
            time.sleep(0.1)
        except FileNotFoundError as exc:
            last = exc
            time.sleep(0.1)
    raise last if last else OSError(f"could not open {port}")


def capture_serial(port: str, duration: float, settle: float = 0.0) -> str:
    """
    Read from a USB CDC port for `duration` seconds.

    Opened non-blocking so a missing DCD can't wedge us, and put into raw mode
    so the line discipline doesn't mangle the output.
    """
    chunks: list[bytes] = []
    fd = None
    try:
        fd = open_port(port)
        # Start the clock only once the port is actually open, so time spent
        # waiting out the udev race doesn't come out of the capture window.
        deadline = time.monotonic() + duration
        try:
            tty.setraw(fd)
        except termios.error:
            pass  # not all stacks allow this; reading still works
        if settle:
            time.sleep(settle)
        while time.monotonic() < deadline and not stop_event.is_set():
            ready, _, _ = select.select([fd], [], [], 0.2)
            if not ready:
                continue
            try:
                data = os.read(fd, 4096)
            except BlockingIOError:
                continue
            except OSError:
                break  # device yanked
            if data:
                chunks.append(data)
    except OSError as exc:
        return f"<<open failed: {exc}>>"
    finally:
        if fd is not None:
            try:
                os.close(fd)
            except OSError:
                pass
    return b"".join(chunks).decode("utf-8", errors="replace")


def evaluate(text: str, expect_version: str | None, require_sensor: bool,
             min_readings: int) -> tuple[str, str, dict]:
    """Turn captured serial output into a pass/fail verdict."""
    info: dict = {"version": None, "hw_rev": None, "sensor": "unknown", "temperature": None}

    banner = RE_BANNER.search(text)
    if banner:
        info["version"], info["hw_rev"] = banner.group(1), banner.group(2)

    ext = [float(v) for v in RE_READING_EXT.findall(text)]
    internal = [float(v) for v in RE_READING_INT.findall(text)]

    if RE_SENSOR_OK.search(text) or ext:
        info["sensor"] = "present"
    elif RE_SENSOR_MISSING.search(text) or internal:
        info["sensor"] = "absent"

    if not text.strip():
        return "FAIL", "no serial output (firmware not running?)", info
    if text.startswith("<<open failed"):
        return "FAIL", text.strip("<>"), info

    readings = ext if info["sensor"] == "present" else internal
    if len(readings) < min_readings:
        return "FAIL", f"only {len(readings)} reading(s); main loop may be stalled", info
    info["temperature"] = readings[-1]

    # A banner is best-effort: it prints once, ~1 s after boot, and can be
    # missed if the port is opened late. Only enforce it when it was seen.
    if expect_version and info["version"] and info["version"] != expect_version:
        return "FAIL", f"version {info['version']}, expected {expect_version}", info

    if info["sensor"] == "present":
        lo, hi = AMBIENT_MIN, AMBIENT_MAX
        if not (lo <= info["temperature"] <= hi):
            return "FAIL", f"ambient {info['temperature']} F outside {lo}-{hi} F", info
        return "PASS", f"DS18B20 {info['temperature']:.1f} F", info

    if info["sensor"] == "absent":
        if require_sensor:
            return "FAIL", "no DS18B20 detected (--require-sensor)", info
        lo, hi = DIE_MIN, DIE_MAX
        if not (lo <= info["temperature"] <= hi):
            return "FAIL", f"die temp {info['temperature']} F outside {lo}-{hi} F", info
        return "PASS_NO_SENSOR", f"no DS18B20; die {info['temperature']:.1f} F", info

    return "FAIL", "could not determine sensor state", info


def handle_board(board: Board, args) -> None:
    """Flash and verify a single board. Runs in its own thread."""
    try:
        _handle_board(board, args)
    finally:
        if board.serial:
            with active_lock:
                active.discard(board.serial)


def _handle_board(board: Board, args) -> None:
    log(f"[{board.label}] detected in BOOTSEL - flashing...")

    ok, err = flash(board, args.firmware, args.flash_timeout)
    if not ok:
        record(Result(board.label, board.serial, "FAIL", f"flash failed: {err}"))
        return

    # Wait for it to come back as a serial device, then open it fast so the
    # one-shot boot banner isn't missed.
    port = None
    deadline = time.monotonic() + args.enumerate_timeout
    while time.monotonic() < deadline and not stop_event.is_set():
        port = find_serial_port(board.serial, board.devpath)
        if port:
            break
        time.sleep(0.05)

    if not port:
        record(Result(board.label, board.serial, "FAIL",
                      f"flashed OK but no serial port within {args.enumerate_timeout}s",
                      sensor="unknown"))
        return

    log(f"[{board.label}] flashed; reading {port}...")
    text = capture_serial(port, args.capture_seconds)
    status, detail, info = evaluate(text, args.expect_version, args.require_sensor,
                                    args.min_readings)
    record(Result(board.label, board.serial, status, detail,
                  version=info["version"], hw_rev=info["hw_rev"],
                  sensor=info["sensor"], temperature=info["temperature"]))


def record(res: Result) -> None:
    with results_lock:
        results.append(res)
    icon = {"PASS": "PASS ", "PASS_NO_SENSOR": "PASS*"}.get(res.status, "FAIL ")
    ver = f" v{res.version}" if res.version else ""
    log(f"  [{icon}] {res.label}{ver} - {res.detail}")


def write_log(path: str) -> None:
    new = not os.path.exists(path)
    with open(path, "a", newline="") as fh:
        w = csv.writer(fh)
        if new:
            w.writerow(["timestamp", "board_serial", "port", "status", "detail",
                        "fw_version", "hw_rev", "sensor", "temp_f"])
        with results_lock:
            for r in results:
                w.writerow([r.when, r.serial or "", r.label, r.status, r.detail,
                            r.version or "", r.hw_rev or "", r.sensor,
                            "" if r.temperature is None else r.temperature])


def summarise() -> int:
    with results_lock:
        total = len(results)
        passed = sum(1 for r in results if r.ok)
        no_sensor = sum(1 for r in results if r.status == "PASS_NO_SENSOR")
        failed = [r for r in results if not r.ok]
    print("\n" + "=" * 62)
    print(f"  {total} board(s) processed - {passed} passed, {len(failed)} failed")
    if no_sensor:
        print(f"  ({no_sensor} passed without a DS18B20 fitted)")
    for r in failed:
        print(f"    FAIL  {r.label}: {r.detail}")
    print("=" * 62)
    return 1 if failed else 0


def default_firmware() -> str | None:
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    candidates = sorted(glob.glob(os.path.join(here, "firmware", "thermometer-v*.uf2")))
    return candidates[-1] if candidates else None


def main() -> int:
    ap = argparse.ArgumentParser(description="Flash and test BCCC Temperature SAO boards.")
    ap.add_argument("-f", "--firmware", help="UF2 image (default: newest in firmware/)")
    ap.add_argument("--expect-version", help="fail if the reported version differs")
    ap.add_argument("--require-sensor", action="store_true",
                    help="treat a missing DS18B20 as a failure")
    # The firmware boot splash (8.8.8. self-test -> DEF -> C0n -> LoL, ~4 s) runs
    # before the first temperature reading, so the window must comfortably clear
    # the splash and still capture min_readings samples (~0.86 s apart).
    ap.add_argument("--capture-seconds", type=float, default=10.0,
                    help="how long to listen on the serial port (default 10)")
    ap.add_argument("--min-readings", type=int, default=3,
                    help="temperature lines required to pass (default 3)")
    ap.add_argument("--flash-timeout", type=int, default=60)
    ap.add_argument("--enumerate-timeout", type=float, default=15.0)
    ap.add_argument("--poll-interval", type=float, default=0.25)
    ap.add_argument("--log", help="append results to this CSV file")
    ap.add_argument("--once", action="store_true",
                    help="process boards already plugged in, then exit")
    args = ap.parse_args()

    if not args.firmware:
        args.firmware = default_firmware()
    if not args.firmware or not os.path.isfile(args.firmware):
        print(f"error: firmware image not found: {args.firmware}", file=sys.stderr)
        return 2

    print(f"Firmware : {args.firmware}")
    print(f"Sensor   : {'required' if args.require_sensor else 'optional (no DS18B20 is OK)'}")
    if args.expect_version:
        print(f"Version  : must report {args.expect_version}")
    print("\nPlug in boards (BOOTSEL held, or blank from the factory). Ctrl-C when done.\n")

    seen: set[str] = set()
    threads: list[threading.Thread] = []

    try:
        while not stop_event.is_set():
            for board in find_bootsel_boards():
                # Don't re-dispatch the same enumeration...
                if board.instance_key in seen:
                    continue
                # ...and don't start a second worker for a board already in
                # flight: flashing makes it re-enumerate under a new devnum,
                # which would otherwise look like a brand new board. Keyed on
                # the chip serial, which survives the reboot. Once the worker
                # finishes the serial is released, so a re-plug can retry.
                if board.serial:
                    with active_lock:
                        if board.serial in active:
                            continue
                        active.add(board.serial)
                seen.add(board.instance_key)
                t = threading.Thread(target=handle_board, args=(board, args), daemon=True)
                t.start()
                threads.append(t)
            if args.once:
                # Give dispatch a moment, then drain.
                time.sleep(1.0)
                if all(not t.is_alive() for t in threads):
                    break
            time.sleep(args.poll_interval)
    except KeyboardInterrupt:
        print("\nstopping...")
        stop_event.set()

    for t in threads:
        t.join(timeout=args.flash_timeout + args.enumerate_timeout + args.capture_seconds + 5)

    if args.log:
        write_log(args.log)
        print(f"\nresults appended to {args.log}")

    return summarise()


if __name__ == "__main__":
    sys.exit(main())
