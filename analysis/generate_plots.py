#!/usr/bin/env python3
"""
Generate WCET and schedulability plots from captured logs.

Inputs:
- UART capture log containing WCET_CSV and optionally WCET_HIST lines.
- Optional task-set CSV for RTA margin chart.

Example:
  python analysis/generate_plots.py --log analysis/wcet_run2.log --task-csv analysis/task_set_measured.csv --out-dir analysis/plots
"""

import argparse
import csv
import math
from pathlib import Path
from typing import Dict, List, Optional, Tuple


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


def cycles_to_ms(cycles: float, cpu_hz: float) -> float:
    return (cycles / cpu_hz) * 1000.0


def parse_wcet_rows(log_path: Path) -> List[Dict[str, int]]:
    rows: List[Dict[str, int]] = []
    with log_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            idx = line.find("WCET_CSV,")
            if idx < 0:
                continue
            parts = line[idx:].strip().split(",")
            if len(parts) != (1 + len(WCET_KEYS)):
                continue
            try:
                vals = [int(x) for x in parts[1:]]
            except ValueError:
                continue
            rows.append(dict(zip(WCET_KEYS, vals)))
    return rows


def parse_latest_hist(log_path: Path) -> Optional[Tuple[int, List[int], List[int], List[int]]]:
    latest_count = -1
    bins_idx: List[int] = []
    bins_lo: List[int] = []
    bins_count: List[int] = []

    with log_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            idx = line.find("WCET_HIST,")
            if idx < 0:
                continue
            parts = line[idx:].strip().split(",")
            if len(parts) != 6:
                continue
            try:
                count = int(parts[1])
                bidx = int(parts[2])
                blo = int(parts[3])
                bcount = int(parts[5])
            except ValueError:
                continue

            if count > latest_count:
                latest_count = count
                bins_idx = [bidx]
                bins_lo = [blo]
                bins_count = [bcount]
            elif count == latest_count:
                bins_idx.append(bidx)
                bins_lo.append(blo)
                bins_count.append(bcount)

    if latest_count < 0:
        return None

    ordered = sorted(zip(bins_idx, bins_lo, bins_count), key=lambda x: x[0])
    return latest_count, [x[0] for x in ordered], [x[1] for x in ordered], [x[2] for x in ordered]


def liu_layland_bound(n: int) -> float:
    if n <= 0:
        return 0.0
    return n * ((2.0 ** (1.0 / n)) - 1.0)


def parse_last_wcet(log_path: Path) -> Optional[Dict[str, int]]:
    rows = parse_wcet_rows(log_path)
    return rows[-1] if rows else None


def load_tasks(task_csv: Path, wcet: Dict[str, int], ci_mode: str, cpu_hz: float) -> List[Dict[str, float]]:
    def choose_ci_cycles(mode: str) -> int:
        m = {
            "max": wcet["max"],
            "bound": wcet["bound"],
            "p99": wcet["p99"],
            "max_unpreempted": wcet["max_unpreempted"],
        }
        return int(m[mode])

    tasks: List[Dict[str, float]] = []
    with task_csv.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            name = row["task"].strip()
            pri = int(row["priority"])
            period = float(row["period_ms"])
            deadline = float(row.get("deadline_ms") or period)
            ci_ms_field = (row.get("ci_ms") or "").strip()
            ci_cycles_field = (row.get("ci_cycles") or "").strip()

            if ci_ms_field:
                ci_ms = float(ci_ms_field)
            elif ci_cycles_field:
                ci_ms = cycles_to_ms(float(ci_cycles_field), cpu_hz)
            elif name.lower() == "inference":
                ci_ms = cycles_to_ms(choose_ci_cycles(ci_mode), cpu_hz)
            else:
                raise ValueError(f"Task {name}: missing ci_ms/ci_cycles")

            tasks.append(
                {
                    "task": name,
                    "priority": pri,
                    "period_ms": period,
                    "deadline_ms": deadline,
                    "ci_ms": ci_ms,
                }
            )
    return tasks


def rta(tasks: List[Dict[str, float]]) -> List[Dict[str, float]]:
    ordered = sorted(tasks, key=lambda x: int(x["priority"]))
    out: List[Dict[str, float]] = []

    for i, t in enumerate(ordered):
        hp = ordered[:i]
        r_prev = t["ci_ms"]
        for _ in range(256):
            interf = 0.0
            for h in hp:
                interf += math.ceil(r_prev / h["period_ms"]) * h["ci_ms"]
            r_next = t["ci_ms"] + interf
            if abs(r_next - r_prev) < 1e-9:
                r_prev = r_next
                break
            if r_next > t["deadline_ms"]:
                r_prev = r_next
                break
            r_prev = r_next

        out.append(
            {
                "task": t["task"],
                "priority": t["priority"],
                "ri_ms": r_prev,
                "di_ms": t["deadline_ms"],
                "margin_ms": t["deadline_ms"] - r_prev,
                "util": t["ci_ms"] / t["period_ms"],
            }
        )

    return out


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Generate WCET and schedulability plots")
    p.add_argument("--log", required=True, help="UART capture log path")
    p.add_argument("--task-csv", default="", help="Optional task set CSV for RTA plots")
    p.add_argument("--cpu-hz", type=float, default=50_000_000.0, help="CPU frequency")
    p.add_argument("--ci", choices=["max", "bound", "p99", "max_unpreempted"], default="bound")
    p.add_argument("--out-dir", default="analysis/plots", help="Output folder")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    log_path = Path(args.log)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not log_path.exists():
        print(f"[error] log not found: {log_path}")
        return 2

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("[error] matplotlib is required. Install with: pip install matplotlib")
        return 2

    wcet_rows = parse_wcet_rows(log_path)
    if not wcet_rows:
        print("[error] no WCET_CSV rows found in log")
        return 2

    counts = [r["count"] for r in wcet_rows]
    min_ms = [cycles_to_ms(r["min"], args.cpu_hz) for r in wcet_rows]
    mean_ms = [cycles_to_ms(r["mean"], args.cpu_hz) for r in wcet_rows]
    p99_ms = [cycles_to_ms(r["p99"], args.cpu_hz) for r in wcet_rows]
    max_ms = [cycles_to_ms(r["max"], args.cpu_hz) for r in wcet_rows]
    bound_ms = [cycles_to_ms(r["bound"], args.cpu_hz) for r in wcet_rows]
    pre_ratio = []
    for r in wcet_rows:
        denom = max(1, r["preempted"] + r["unpreempted"])
        pre_ratio.append(100.0 * r["preempted"] / denom)

    plt.figure(figsize=(10, 5))
    plt.plot(counts, min_ms, label="min")
    plt.plot(counts, mean_ms, label="mean")
    plt.plot(counts, p99_ms, label="p99")
    plt.plot(counts, max_ms, label="max")
    plt.plot(counts, bound_ms, label="bound")
    plt.xlabel("Inference sample count")
    plt.ylabel("Time (ms)")
    plt.title("Inference Timing Trend")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    p1 = out_dir / "wcet_trend.png"
    plt.savefig(p1, dpi=140)
    plt.close()

    plt.figure(figsize=(8, 4))
    plt.plot(counts, pre_ratio, color="tab:red")
    plt.ylim(0, 100)
    plt.xlabel("Inference sample count")
    plt.ylabel("Preempted ratio (%)")
    plt.title("Preemption Tag Ratio Trend")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    p2 = out_dir / "preemption_ratio.png"
    plt.savefig(p2, dpi=140)
    plt.close()

    hist = parse_latest_hist(log_path)
    p3 = None
    if hist is not None:
        latest_count, _, bins_lo, bins_count = hist
        x_ms = [cycles_to_ms(v, args.cpu_hz) for v in bins_lo]
        plt.figure(figsize=(10, 4))
        plt.bar(x_ms, bins_count, width=(x_ms[1] - x_ms[0]) if len(x_ms) > 1 else 0.1)
        plt.xlabel("Cycle bin lower edge (ms)")
        plt.ylabel("Samples")
        plt.title(f"WCET Histogram (latest dump at count={latest_count})")
        plt.tight_layout()
        p3 = out_dir / "wcet_hist_latest.png"
        plt.savefig(p3, dpi=140)
        plt.close()

    p4 = None
    if args.task_csv:
        wcet_last = parse_last_wcet(log_path)
        if wcet_last is None:
            print("[warn] skipped RTA plots: no final WCET")
        else:
            tasks = load_tasks(Path(args.task_csv), wcet_last, args.ci, args.cpu_hz)
            rta_rows = rta(tasks)
            labels = [r["task"] for r in rta_rows]
            margins = [r["margin_ms"] for r in rta_rows]

            plt.figure(figsize=(9, 4))
            colors = ["tab:green" if m >= 0 else "tab:red" for m in margins]
            plt.bar(labels, margins, color=colors)
            plt.axhline(0.0, color="black", linewidth=1)
            plt.ylabel("Deadline margin (ms)")
            plt.title("RTA Margin by Task")
            plt.tight_layout()
            p4 = out_dir / "rta_margins.png"
            plt.savefig(p4, dpi=140)
            plt.close()

            util = sum(float(t["ci_ms"]) / float(t["period_ms"]) for t in tasks)
            ub = liu_layland_bound(len(tasks))
            plt.figure(figsize=(6, 4))
            plt.bar(["U_total", "U_bound"], [util, ub], color=["tab:blue", "tab:orange"])
            plt.ylabel("Utilization")
            plt.title("Utilization vs Liu-Layland Bound")
            plt.tight_layout()
            p5 = out_dir / "utilization_vs_bound.png"
            plt.savefig(p5, dpi=140)
            plt.close()
        
    print("[ok] generated plots:")
    print(f"- {p1}")
    print(f"- {p2}")
    if p3 is not None:
        print(f"- {p3}")
    if p4 is not None:
        print(f"- {p4}")
    if args.task_csv:
        print(f"- {out_dir / 'utilization_vs_bound.png'}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
