#!/usr/bin/env python3
"""
Parse WCET UART CSV lines and generate RM/RTA-ready summaries.

Expected firmware lines:
  WCET_CSV,count,warmup_discarded,last,min,max,max_unpreempted,mean,p99,bound,overhead,bin_width,hist_overflow,preempted,unpreempted,uptime_ms

Optional task-set CSV columns for RTA:
  task,priority,period_ms,deadline_ms,ci_cycles,ci_ms,ci_source

Priority convention in task-set CSV:
  lower numeric value = higher priority (classic RM ordering).
"""

import argparse
import csv
import math
from dataclasses import dataclass
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


@dataclass
class Task:
    task: str
    priority: int
    period_ms: float
    deadline_ms: float
    ci_ms: float
    ci_source: str


@dataclass
class RtaResult:
    task: str
    priority: int
    period_ms: float
    deadline_ms: float
    ci_ms: float
    ri_ms: float
    schedulable: bool


def cycles_to_ms(cycles: float, cpu_hz: float) -> float:
    return (cycles / cpu_hz) * 1000.0


def parse_last_wcet_csv(log_path: Path) -> Optional[Dict[str, int]]:
    last_values: Optional[List[int]] = None

    with log_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "WCET_CSV," not in line:
                continue
            idx = line.find("WCET_CSV,")
            payload = line[idx:].strip()
            parts = payload.split(",")
            if len(parts) != (1 + len(WCET_KEYS)):
                continue
            try:
                vals = [int(x) for x in parts[1:]]
            except ValueError:
                continue
            last_values = vals

    if last_values is None:
        return None

    return dict(zip(WCET_KEYS, last_values))


def choose_ci_cycles(wcet: Dict[str, int], mode: str) -> int:
    mapping = {
        "max": "max",
        "bound": "bound",
        "p99": "p99",
        "max_unpreempted": "max_unpreempted",
    }
    return int(wcet[mapping[mode]])


def build_default_inference_task(
    wcet: Dict[str, int],
    ci_mode: str,
    cpu_hz: float,
    sample_period_ms: float,
    window_size: int,
) -> Task:
    ci_cycles = choose_ci_cycles(wcet, ci_mode)
    ci_ms = cycles_to_ms(ci_cycles, cpu_hz)
    period_ms = sample_period_ms * float(window_size)
    return Task(
        task="Inference",
        priority=1,
        period_ms=period_ms,
        deadline_ms=period_ms,
        ci_ms=ci_ms,
        ci_source=f"inference.{ci_mode}",
    )


def load_task_set(task_csv: Path, wcet: Dict[str, int], ci_mode: str, cpu_hz: float) -> List[Task]:
    tasks: List[Task] = []

    with task_csv.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            name = row["task"].strip()
            pri = int(row["priority"])
            period = float(row["period_ms"])
            deadline = float(row.get("deadline_ms") or period)
            source = (row.get("ci_source") or "task_csv").strip()

            ci_ms_field = (row.get("ci_ms") or "").strip()
            ci_cycles_field = (row.get("ci_cycles") or "").strip()

            if ci_ms_field:
                ci_ms = float(ci_ms_field)
            elif ci_cycles_field:
                ci_ms = cycles_to_ms(float(ci_cycles_field), cpu_hz)
            elif name.lower() == "inference":
                ci_ms = cycles_to_ms(float(choose_ci_cycles(wcet, ci_mode)), cpu_hz)
                source = f"inference.{ci_mode}"
            else:
                raise ValueError(f"Task {name}: missing ci_ms/ci_cycles")

            tasks.append(
                Task(
                    task=name,
                    priority=pri,
                    period_ms=period,
                    deadline_ms=deadline,
                    ci_ms=ci_ms,
                    ci_source=source,
                )
            )

    return tasks


def liu_layland_bound(n: int) -> float:
    if n <= 0:
        return 0.0
    return n * ((2.0 ** (1.0 / n)) - 1.0)


def response_time_analysis(tasks: List[Task]) -> List[RtaResult]:
    by_pri = sorted(tasks, key=lambda t: t.priority)
    results: List[RtaResult] = []

    for i, task in enumerate(by_pri):
        hp = by_pri[:i]
        r_prev = task.ci_ms

        # Fixed-point iteration: R = C + sum ceil(R/Tj)*Cj for j in hp(i)
        converged = False
        for _ in range(256):
            interference = 0.0
            for h in hp:
                interference += math.ceil(r_prev / h.period_ms) * h.ci_ms
            r_next = task.ci_ms + interference

            if abs(r_next - r_prev) < 1e-9:
                converged = True
                r_prev = r_next
                break

            if r_next > task.deadline_ms:
                r_prev = r_next
                break

            r_prev = r_next

        ri = r_prev
        ok = converged and (ri <= task.deadline_ms)
        if (not converged) and (ri <= task.deadline_ms):
            ok = True

        results.append(
            RtaResult(
                task=task.task,
                priority=task.priority,
                period_ms=task.period_ms,
                deadline_ms=task.deadline_ms,
                ci_ms=task.ci_ms,
                ri_ms=ri,
                schedulable=ok,
            )
        )

    return results


def print_wcet_summary(wcet: Dict[str, int], cpu_hz: float) -> None:
    count = wcet["count"]
    pre = wcet["preempted"]
    unpre = wcet["unpreempted"]
    denom = max(1, pre + unpre)
    pre_ratio = (100.0 * pre) / denom

    print("=== WCET Summary (last reported snapshot) ===")
    for k in ["count", "warmup_discarded", "min", "mean", "p99", "max", "max_unpreempted", "bound", "overhead", "hist_overflow", "preempted", "unpreempted"]:
        v = wcet[k]
        print(f"{k:16s}: {v:10d} cycles  ({cycles_to_ms(v, cpu_hz):8.3f} ms)" if k in {"min", "mean", "p99", "max", "max_unpreempted", "bound", "overhead"} else f"{k:16s}: {v}")
    print(f"preempted_ratio  : {pre_ratio:.2f}%")
    print()


def confidence_notes(wcet: Dict[str, int]) -> List[str]:
    notes: List[str] = []

    if wcet["count"] < 500:
        notes.append("sample count is low; target at least 1,000-10,000 windows for stronger tails")
    if wcet["hist_overflow"] > 0:
        notes.append("histogram overflow > 0; increase bin range for trustworthy p99 binning")

    denom = max(1, wcet["preempted"] + wcet["unpreempted"])
    pre_ratio = wcet["preempted"] / denom
    if pre_ratio > 0.10:
        notes.append("more than 10% samples were preempted; treat max as inflated by interference")

    if wcet["max_unpreempted"] == 0:
        notes.append("no non-preempted samples observed; cannot separate pure C_i from interference")

    if not notes:
        notes.append("no major quality flags detected in the last snapshot")

    return notes


def print_rm_summary(tasks: List[Task], rta: List[RtaResult]) -> None:
    util = sum(t.ci_ms / t.period_ms for t in tasks)
    ub = liu_layland_bound(len(tasks))

    print("=== RM Input Table ===")
    print("task,priority,period_ms,deadline_ms,C_i_ms,util_i,ci_source")
    for t in sorted(tasks, key=lambda x: x.priority):
        ui = t.ci_ms / t.period_ms
        print(f"{t.task},{t.priority},{t.period_ms:.3f},{t.deadline_ms:.3f},{t.ci_ms:.3f},{ui:.6f},{t.ci_source}")

    print()
    print("=== Utilization Check ===")
    print(f"U_total={util:.6f}")
    print(f"U_bound(n={len(tasks)})={ub:.6f}")
    print("util_test=PASS" if util <= ub else "util_test=NOT_SUFFICIENT (run RTA)")
    print()

    print("=== Response-Time Analysis ===")
    print("task,priority,R_i_ms,D_i_ms,margin_ms,schedulable")
    for r in rta:
        margin = r.deadline_ms - r.ri_ms
        print(f"{r.task},{r.priority},{r.ri_ms:.3f},{r.deadline_ms:.3f},{margin:.3f},{'YES' if r.schedulable else 'NO'}")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Generate WCET and RM/RTA report from UART logs")
    p.add_argument("--log", required=True, help="Captured UART log file")
    p.add_argument("--cpu-hz", type=float, default=50_000_000.0, help="CPU clock in Hz")
    p.add_argument("--ci", choices=["max", "bound", "p99", "max_unpreempted"], default="bound",
                   help="Inference C_i source for RM when inferred from WCET")
    p.add_argument("--sample-period-ms", type=float, default=20.0,
                   help="Sensor sample period used to derive inference period")
    p.add_argument("--window-size", type=int, default=50,
                   help="Samples per inference window")
    p.add_argument("--task-csv", default="", help="Optional task-set CSV for full RTA")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    log_path = Path(args.log)

    if not log_path.exists():
        print(f"[error] log file not found: {log_path}")
        return 2

    wcet = parse_last_wcet_csv(log_path)
    if wcet is None:
        print("[error] no valid WCET_CSV line found in log")
        return 2

    print_wcet_summary(wcet, args.cpu_hz)

    print("=== Confidence Notes ===")
    for note in confidence_notes(wcet):
        print(f"- {note}")
    print()

    if args.task_csv:
        tasks = load_task_set(Path(args.task_csv), wcet, args.ci, args.cpu_hz)
    else:
        tasks = [
            build_default_inference_task(
                wcet,
                args.ci,
                args.cpu_hz,
                args.sample_period_ms,
                args.window_size,
            )
        ]

    rta = response_time_analysis(tasks)
    print_rm_summary(tasks, rta)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
