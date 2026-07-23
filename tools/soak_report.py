#!/usr/bin/env python3
"""
Summarise burn-in logs produced by soak_log.py.

Reports, per board: how long it ran, temperature range and drift, how many
readings arrived, any gaps in the stream, and any disconnects or unexpected
reboots.

    tools/soak_report.py                  # read ./soak-logs
    tools/soak_report.py -o /tmp/soak     # read another directory
    tools/soak_report.py --gap 10         # flag stream gaps over 10 s
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import sys
from datetime import datetime

RE_LINE = re.compile(r"^(?P<ts>\S+) (?P<body>.*)$")
RE_TEMP = re.compile(r"(?:DS18B20|Internal):\s*(-?\d+\.?\d*)\s*F")
RE_BANNER = re.compile(r"firmware v([0-9A-Za-z.+-]+)")


def parse_ts(text: str) -> datetime | None:
    try:
        return datetime.fromisoformat(text)
    except ValueError:
        return None


def report(path: str, gap_threshold: float) -> bool:
    serial = os.path.splitext(os.path.basename(path))[0]
    temps: list[float] = []
    stamps: list[datetime] = []
    events: list[str] = []
    reboots = 0
    first = last = None
    version = None
    sensor_lines = 0
    internal_lines = 0

    with open(path) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("#"):
                continue
            m = RE_LINE.match(line)
            if not m:
                continue
            ts = parse_ts(m.group("ts"))
            body = m.group("body")
            if ts:
                first = first or ts
                last = ts

            if body.startswith("!!") or body.startswith("**"):
                events.append(f"{m.group('ts')} {body}")
                continue

            banner = RE_BANNER.search(body)
            if banner:
                version = banner.group(1)
                reboots += 1

            temp = RE_TEMP.search(body)
            if temp:
                temps.append(float(temp.group(1)))
                if ts:
                    stamps.append(ts)
                if "DS18B20" in body:
                    sensor_lines += 1
                else:
                    internal_lines += 1

    # Find gaps in the reading stream.
    gaps = []
    for a, b in zip(stamps, stamps[1:]):
        delta = (b - a).total_seconds()
        if delta > gap_threshold:
            gaps.append((a, delta))

    duration = (last - first).total_seconds() if first and last else 0.0
    healthy = bool(temps) and not gaps and not any("!!" in e for e in events)
    # A reboot mid-run means the banner printed more than once.
    unexpected_reboot = reboots > 1
    if unexpected_reboot:
        healthy = False

    print(f"\n=== {serial} ===")
    print(f"  duration    : {duration/3600:.2f} h ({duration:.0f} s)")
    print(f"  readings    : {len(temps)}"
          + (f"  (DS18B20 {sensor_lines}, internal {internal_lines})" if temps else ""))
    if version:
        print(f"  firmware    : v{version}")
    if temps:
        drift = temps[-1] - temps[0]
        print(f"  temperature : min {min(temps):.1f} F, max {max(temps):.1f} F, "
              f"first {temps[0]:.1f} F, last {temps[-1]:.1f} F (drift {drift:+.1f} F)")
        if duration > 0:
            print(f"  rate        : {len(temps)/duration:.2f} readings/s")
    else:
        print("  temperature : NO READINGS")

    if unexpected_reboot:
        print(f"  !! boot banner seen {reboots} times - board rebooted mid-soak")
    if gaps:
        print(f"  !! {len(gaps)} gap(s) over {gap_threshold}s in the reading stream:")
        for when, delta in gaps[:10]:
            print(f"       {when.isoformat(timespec='seconds')}  {delta:.1f}s")
        if len(gaps) > 10:
            print(f"       ... and {len(gaps)-10} more")
    for e in events:
        marker = "!!" if "!!" in e else "  "
        print(f"  {marker} {e}")

    print(f"  VERDICT     : {'HEALTHY' if healthy else 'NEEDS REVIEW'}")
    return healthy


def main() -> int:
    ap = argparse.ArgumentParser(description="Summarise BCCC SAO burn-in logs.")
    ap.add_argument("-o", "--outdir", default="soak-logs")
    ap.add_argument("--gap", type=float, default=10.0,
                    help="flag gaps longer than this many seconds (default 10)")
    args = ap.parse_args()

    logs = sorted(glob.glob(os.path.join(args.outdir, "*.log")))
    if not logs:
        print(f"no logs in {args.outdir}/", file=sys.stderr)
        return 1

    ok = [report(p, args.gap) for p in logs]
    print(f"\n{sum(ok)}/{len(ok)} board(s) healthy")
    return 0 if all(ok) else 1


if __name__ == "__main__":
    sys.exit(main())
