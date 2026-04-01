#!/usr/bin/env python3
"""
Capture TM4C UART output to a timestamped text log for offline WCET/RM analysis.

Example:
  python capture_uart.py --port COM5 --baud 115200 --out wcet_run1.log --duration-s 600
"""

import argparse
import datetime as dt
import sys
import time


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Capture UART lines to a log file")
    p.add_argument("--port", required=True, help="Serial port, e.g. COM5")
    p.add_argument("--baud", type=int, default=115200, help="Baud rate")
    p.add_argument("--out", default="wcet_capture.log", help="Output log file path")
    p.add_argument("--duration-s", type=float, default=0.0,
                   help="Capture duration in seconds (0 = until Ctrl+C)")
    p.add_argument("--echo", action="store_true", help="Echo lines to console")
    return p.parse_args()


def main() -> int:
    args = parse_args()

    try:
        import serial
    except ImportError:
        print("[error] pyserial is required. Install with: pip install pyserial")
        return 2

    try:
        ser = serial.Serial(args.port, baudrate=args.baud, timeout=0.25)
    except Exception as exc:
        print(f"[error] failed opening {args.port}: {exc}")
        return 2

    start = time.monotonic()
    line_count = 0

    print(f"[capture] port={args.port} baud={args.baud} out={args.out}")
    if args.duration_s > 0:
        print(f"[capture] duration={args.duration_s:.1f}s")
    else:
        print("[capture] running until Ctrl+C")

    try:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            while True:
                if args.duration_s > 0 and (time.monotonic() - start) >= args.duration_s:
                    break

                raw = ser.readline()
                if not raw:
                    continue

                ts = dt.datetime.now().isoformat(timespec="milliseconds")
                text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                f.write(f"{ts} {text}\n")
                line_count += 1

                if args.echo:
                    print(text)

    except KeyboardInterrupt:
        print("[capture] interrupted by user")
    finally:
        ser.close()

    elapsed = time.monotonic() - start
    print(f"[capture] done lines={line_count} elapsed_s={elapsed:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
