#!/usr/bin/env python3
"""
Burn-in logger for the BCCC Temperature SAO.

Records timestamped serial output from every running board, one log file per
board (named by chip serial). Disconnects and reconnects are logged as events
rather than silently ignored, because a board dropping off the USB bus or
spontaneously rebooting is exactly the failure a soak test exists to catch.

    tools/soak_log.py                 # log all running boards to ./soak-logs
    tools/soak_log.py -o /tmp/soak    # choose the output directory

Runs until interrupted (Ctrl-C / SIGTERM). Analyse with soak_report.py.
"""

from __future__ import annotations

import argparse
import glob
import os
import select
import signal
import sys
import termios
import threading
import time
import tty
from datetime import datetime

VID_RASPBERRYPI = "2e8a"
PID_APP = "0009"

stop = threading.Event()


def now() -> str:
    return datetime.now().isoformat(timespec="milliseconds")


def find_boards() -> dict[str, str]:
    """Map chip serial -> /dev/ttyACM* for every running board."""
    found = {}
    for tty_path in glob.glob("/sys/class/tty/ttyACM*"):
        name = os.path.basename(tty_path)
        try:
            usbdev = os.path.dirname(os.path.realpath(os.path.join(tty_path, "device")))
            if open(os.path.join(usbdev, "idVendor")).read().strip() != VID_RASPBERRYPI:
                continue
            serial = open(os.path.join(usbdev, "serial")).read().strip()
        except OSError:
            continue
        found[serial] = f"/dev/{name}"
    return found


def port_for(serial: str) -> str | None:
    return find_boards().get(serial)


def log_board(serial: str, outdir: str) -> None:
    """Follow one board for the whole run, surviving disconnects."""
    path = os.path.join(outdir, f"{serial}.log")
    with open(path, "a", buffering=1) as out:
        out.write(f"# {now()} SOAK START serial={serial}\n")
        connected = False
        ever_connected = False
        fd = None
        pending = b""
        while not stop.is_set():
            if fd is None:
                port = port_for(serial)
                if port is None:
                    if connected:
                        out.write(f"{now()} !! DISCONNECTED (no serial port)\n")
                        connected = False
                    stop.wait(1.0)
                    continue
                try:
                    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
                    try:
                        tty.setraw(fd)
                    except termios.error:
                        pass
                    event = "RECONNECTED" if ever_connected else "CONNECTED"
                    out.write(f"{now()} ** {event} on {port}\n")
                    connected = True
                    ever_connected = True
                except OSError as exc:
                    out.write(f"{now()} !! OPEN FAILED {port}: {exc}\n")
                    stop.wait(1.0)
                    continue

            ready, _, _ = select.select([fd], [], [], 0.5)
            if not ready:
                continue
            try:
                chunk = os.read(fd, 4096)
            except (BlockingIOError, InterruptedError):
                continue
            except OSError as exc:
                out.write(f"{now()} !! READ ERROR: {exc}\n")
                os.close(fd)
                fd = None
                connected = False
                continue
            if not chunk:
                continue

            pending += chunk
            *lines, pending = pending.split(b"\n")
            for raw in lines:
                text = raw.decode("utf-8", errors="replace").rstrip("\r")
                if text:
                    out.write(f"{now()} {text}\n")

        if fd is not None:
            os.close(fd)
        out.write(f"# {now()} SOAK END\n")


def main() -> int:
    ap = argparse.ArgumentParser(description="Burn-in logger for BCCC SAO boards.")
    ap.add_argument("-o", "--outdir", default="soak-logs", help="output directory")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    boards = find_boards()
    if not boards:
        print("no running boards found", file=sys.stderr)
        return 1

    print(f"logging {len(boards)} board(s) to {args.outdir}/")
    for serial, port in boards.items():
        print(f"  {serial} -> {port}")

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: stop.set())

    threads = [threading.Thread(target=log_board, args=(s, args.outdir), daemon=True)
               for s in boards]
    for t in threads:
        t.start()
    while not stop.is_set():
        stop.wait(1.0)
    for t in threads:
        t.join(timeout=5)
    print("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
