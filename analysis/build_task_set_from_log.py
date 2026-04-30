#!/usr/bin/env python3
"""
Build a task-set CSV from captured WCET_CSV and TASK_CSV lines.

Usage:
  python build_task_set_from_log.py --log wcet_run1.log --out task_set_measured.csv

The inference period is modeled explicitly with --infer-period-ms.
Default: 100 ms.
"""

import argparse
from pathlib import Path
from typing import Dict, List, Optional


WCET_KEYS = [
    "count",
    "warmup_discarded",
    "last",
    "min",
    "max",
    "max_unpreempted",
    "mean",
    "p99",
    "bound",
    "overhead",
    "bin_width",
    "hist_overflow",
    "preempted",
    "unpreempted",
    "uptime_ms",
]

TASK_KEYS = [
    "count",
    "sensor_last",
    "sensor_min",
    "sensor_max",
    "sensor_mean",
    "uart_last",
    "uart_min",
    "uart_max",
    "uart_mean",
    "logger_last",
    "logger_min",
    "logger_max",
    "logger_mean",
    "uptime_ms",
]

TASK_ROW_KEYS = [
    "task",
    "priority",
    "period_ms",
    "deadline_ms",
    "max_cycles",
]


def parse_last_tagged_line(log_path: Path, tag: str, keys: List[str]) -> Optional[Dict[str, int]]:
    last_values: Optional[List[int]] = None

    with log_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if tag not in line:
                continue
            idx = line.find(tag)
            payload = line[idx:].strip()
            parts = payload.split(",")
            if len(parts) != (1 + len(keys)):
                continue
            try:
                vals = [int(x) for x in parts[1:]]
            except ValueError:
                continue
            last_values = vals

    if last_values is None:
        return None
    return dict(zip(keys, last_values))


def cycles_to_ms(cycles: int, cpu_hz: float) -> float:
    return (float(cycles) / cpu_hz) * 1000.0


def parse_task_rows(log_path: Path) -> Dict[str, Dict[str, float]]:
    rows: Dict[str, Dict[str, float]] = {}

    with log_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "TASK_CSV," not in line:
                continue

            idx = line.find("TASK_CSV,")
            payload = line[idx:].strip()
            parts = payload.split(",")

            if len(parts) != 6:
                continue
            if parts[1].isdigit():
                continue

            try:
                rows[parts[1]] = {
                    "priority": float(parts[2]),
                    "period_ms": float(parts[3]),
                    "deadline_ms": float(parts[4]),
                    "max_cycles": float(parts[5]),
                }
            except ValueError:
                continue

    return rows


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Build measured task_set CSV from UART log")
    p.add_argument("--log", required=True, help="UART log file")
    p.add_argument("--out", default="task_set_measured.csv", help="Output CSV path")
    p.add_argument("--cpu-hz", type=float, default=80_000_000.0, help="CPU clock in Hz")
    p.add_argument("--sensor-period-ms", type=float, default=20.0, help="Sensor task period")
    p.add_argument("--uart-period-ms", type=float, default=10.0, help="UART task loop period")
    p.add_argument(
        "--infer-period-ms",
        type=float,
        default=100.0,
        help="Inference task period to write into the task set",
    )
    p.add_argument(
        "--allow-uart-overrun",
        action="store_true",
        help="Allow emitting a task set even when measured UART ci_ms exceeds uart-period-ms",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()
    log_path = Path(args.log)

    if not log_path.exists():
        print(f"[error] log file not found: {log_path}")
        return 2

    wcet = parse_last_tagged_line(log_path, "WCET_CSV,", WCET_KEYS)
    task = parse_last_tagged_line(log_path, "TASK_CSV,", TASK_KEYS)
    task_rows = parse_task_rows(log_path)

    if wcet is None:
        print("[error] no WCET_CSV line found")
        return 2
    if task is None and not task_rows:
        print("[error] no TASK_CSV line found")
        print("        Rebuild/flash firmware that includes TASK_CSV output.")
        return 2

    sensor_period = args.sensor_period_ms
    uart_period = args.uart_period_ms
    infer_period = args.infer_period_ms
    logger_period = infer_period

    sensor_ci_ms = cycles_to_ms(task["sensor_max"], args.cpu_hz) if task is not None else 0.0
    uart_ci_ms = cycles_to_ms(task["uart_max"], args.cpu_hz) if task is not None else 0.0
    logger_ci_ms = cycles_to_ms(task["logger_max"], args.cpu_hz) if task is not None else 0.0

    if "Sensor" in task_rows:
        sensor_ci_ms = cycles_to_ms(int(task_rows["Sensor"]["max_cycles"]), args.cpu_hz)
        sensor_period = task_rows["Sensor"]["period_ms"]
    if "UART" in task_rows:
        uart_ci_ms = cycles_to_ms(int(task_rows["UART"]["max_cycles"]), args.cpu_hz)
        uart_period = task_rows["UART"]["period_ms"]
    if "Logger" in task_rows:
        logger_ci_ms = cycles_to_ms(int(task_rows["Logger"]["max_cycles"]), args.cpu_hz)
        logger_period = task_rows["Logger"]["period_ms"]

    if (uart_ci_ms > uart_period) and (not args.allow_uart_overrun):
        print("[error] measured UART ci_ms exceeds configured UART period")
        print(f"        uart_ci_ms={uart_ci_ms:.3f} ms, uart_period_ms={uart_period:.3f} ms")
        print("        This usually means replay/streaming work was captured in UART timing.")
        print("        For RM periodic analysis, rebuild firmware with replay path disabled")
        print("        or rerun with a UART period that matches the measured operating mode.")
        print("        If you intentionally want this task set, rerun with --allow-uart-overrun.")
        return 3

    out = Path(args.out)
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("task,priority,period_ms,deadline_ms,ci_ms,ci_cycles,ci_source\n")
        # Script convention: lower number = higher priority.
        # Keep this aligned to current firmware urgency order.
        f.write(f"Sensor,2,{sensor_period:.3f},{sensor_period:.3f},{sensor_ci_ms:.3f},,measured.task_csv_max\n")
        f.write(f"UART,1,{uart_period:.3f},{uart_period:.3f},{uart_ci_ms:.3f},,measured.task_csv_max\n")
        f.write(f"Inference,3,{infer_period:.3f},{infer_period:.3f},,,from_wcet\n")
        f.write(f"Logger,4,{logger_period:.3f},{logger_period:.3f},{logger_ci_ms:.3f},,measured.task_csv_max\n")

        if "Feedback" in task_rows:
            feedback_row = task_rows["Feedback"]
            feedback_ci_ms = cycles_to_ms(int(feedback_row["max_cycles"]), args.cpu_hz)
            f.write(
                f"Feedback,{int(feedback_row['priority'])},{feedback_row['period_ms']:.3f},"
                f"{feedback_row['deadline_ms']:.3f},{feedback_ci_ms:.3f},,measured.task_csv_max\n"
            )

    print(f"[ok] wrote {out}")
    print("[summary] derived ci_ms from TASK_CSV max cycles:")
    print(f"  Sensor: {sensor_ci_ms:.3f} ms")
    print(f"  UART  : {uart_ci_ms:.3f} ms")
    print(f"  Logger: {logger_ci_ms:.3f} ms")
    print(f"[summary] inference period written to task set: {infer_period:.3f} ms")
    print("[next] run:")
    print(f"  python wcet_rm_report.py --log {log_path} --task-csv {out} --ci bound")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
