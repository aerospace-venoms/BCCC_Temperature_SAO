# Production Test Tools

## `flash_and_test.py` — batch flash + verify station

Watches for RP2350 boards entering BOOTSEL mode, flashes each with the release
firmware, then reopens it as a serial device and confirms the firmware is
actually running. Handles several boards at once, so you can keep plugging in
cables without waiting for each to finish.

```sh
tools/flash_and_test.py                        # newest firmware/*.uf2, run until Ctrl-C
tools/flash_and_test.py --log run.csv          # append a per-board record
tools/flash_and_test.py --expect-version 1.1.0 # fail on a version mismatch
tools/flash_and_test.py --require-sensor       # only once DS18B20s are fitted
tools/flash_and_test.py --once                 # process what's plugged in, then exit
```

### How boards are kept straight in parallel

Every RP2350 reports a unique USB serial number (its chip ID), and it reports
the **same** value in BOOTSEL mode and when running the firmware. After
flashing a board, the tool finds the `/dev/ttyACM*` whose USB serial matches
that board, so results can never be attributed to the wrong cable. If a board
doesn't expose a serial string, it falls back to matching the physical USB port
path, which is also stable across the reboot.

Flashing targets one specific device via `picotool load -x --ser <serial>`, so
nothing depends on which volume auto-mounted where.

**picotool calls are serialised on purpose.** picotool opens candidate USB
devices to read their serial numbers, so two instances running at once make
each other's target look inaccessible — it reports "no accessible RP-series
device ... with serial number X" even though the board is plugged in and
perfectly healthy. Only the flash step holds that lock; it takes a second or
two, while the much slower per-board serial verification still runs fully in
parallel. Don't run a second picotool by hand while a batch is in progress.

### Verdicts

| Result | Meaning |
|---|---|
| `PASS` | Firmware runs, DS18B20 detected, ambient reading plausible (32–120 °F) |
| `PASS*` | Firmware runs, **no DS18B20 fitted** — fell back to internal die temp (40–190 °F) |
| `FAIL` | Flash failed, no serial port appeared, no output, too few readings, bad version, or an implausible temperature |

`PASS*` is a pass on purpose: this is expected before the sensors are attached.
Once they're fitted, add `--require-sensor` and a missing sensor becomes a
failure — that's how you catch an unpopulated or cold-jointed DS18B20.

**Read-back is retried.** The flash (picotool) and the verification (reading the
serial port back) are separate steps, and the *verification* can come up short
for reasons unrelated to a bad board: the port opened a beat late, USB was busy
with another board or the auto-mounted BOOTSEL volume, or the port briefly
resolved to the wrong device. So when a board flashes fine but the read-back is
short (too few readings / no output), the tool re-reads it (`--verify-retries`,
default 2) before calling it a FAIL. A genuinely dead board still yields nothing
across the retries; a healthy board that just lost the race passes. Real
firmware faults — version mismatch, implausible temperature, missing sensor when
required — are final and never retried. If you see many `only N readings` fails,
they are almost always flashed-and-fine boards, not scrap.

The boot banner (`firmware v1.1.0 (HW rev 13)`) prints once, ~1 s after boot.
The tool opens the port as fast as it can to catch it, but treats a missed
banner as non-fatal — the version is only enforced when it was actually seen.

## `soak_log.py` / `soak_report.py` — burn-in testing

For leaving boards running to prove they stay up over hours, not seconds.

```sh
tools/soak_log.py -o soak-logs      # follow every running board; Ctrl-C to stop
tools/soak_report.py -o soak-logs   # summarise the result
```

`soak_log.py` writes one timestamped log per board, named by chip serial. It
logs disconnects and reconnects as explicit events instead of ignoring them —
a board dropping off the USB bus or rebooting on its own is precisely what a
soak test is looking for. Run it detached (`setsid nohup ... &`) to survive a
closed terminal.

`soak_report.py` reports per board: run duration, reading count and rate,
temperature range and drift, gaps in the stream (`--gap`, default 10 s),
disconnect events, and whether the boot banner appeared more than once, which
means the board rebooted mid-run. Verdict is `HEALTHY` or `NEEDS REVIEW`;
exit status is non-zero if any board needs review.

Note that these hold the serial ports open, so stop the logger
(`pkill -f soak_log.py`) before flashing those boards again.

### Requirements

- `picotool` on `PATH` (v2.x). If it reports a permissions error, install its
  udev rules or run the tool with `sudo`.
- Python 3.9+ — standard library only, no pip installs.

### Notes

- Blank boards from the factory enumerate straight into BOOTSEL, so they're
  picked up automatically. An already-programmed board must be put into BOOTSEL
  by hand (hold BOOTSEL/QSPI_SS while plugging in) before it will be re-flashed.
- Re-plugging a board makes the tool treat it as a new unit, so retries just
  work.
- Results go to stdout live; `--log` writes CSV (timestamp, chip serial, port,
  status, version, sensor state, temperature) for traceability across a run.
  Each board's row is appended and flushed the moment it finishes, so an
  interrupted run — even a `kill` or a crash — keeps every board already tested.
