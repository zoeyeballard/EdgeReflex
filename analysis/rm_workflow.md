# RM and WCET Workflow (TM4C123G + FreeRTOS)

## 1) WCET campaign assumptions

- CPU clock fixed at 50 MHz.
- FreeRTOS preemptive scheduler, tick = 1 kHz.
- Inference task release period is approximately:
  - T_infer = 100 ms in the current firmware configuration.
- WCET timing source is DWT cycle counter around inference compute block.
- For production RM campaigns, build firmware with UART replay injection disabled
  and UART heartbeat logging disabled so UART C_i reflects periodic service work
  rather than host-stream replay bursts or infrequent long UART prints.

## 2) What the firmware now reports

From `WCET_CSV`:

- `min`, `mean`, `p99`, `max`, `bound` (cycles)
- `max_unpreempted` (cycles): largest sample where tick did not advance across measured block.
- `preempted`, `unpreempted`: quality indicator for measurement bias.
- `warmup_discarded`: excluded startup samples.
- `hist_overflow`: indicates p99 histogram clipping risk.

From `TASK_CSV`:

- `sensor_max`, `uart_max`, `logger_max` (cycles): non-inference task C_i candidates.
- `*_mean` fields: sanity check for stability.

## 3) Choosing C_i for RM

Use these rules:

- Preferred baseline C_i for inference task: `max_unpreempted`.
- Conservative production C_i: `max(bound, max_unpreempted)` where `bound = max + 12.5%`.
- If `unpreempted == 0`, use `bound` and treat result as provisional.

Rationale:

- `max` can include preemption interference (inflated wall-time).
- `max_unpreempted` better approximates pure execution demand.
- `bound` adds guard margin for unobserved tails.

## 4) RM formulas and exact steps

Given tasks ordered by RM priority (shorter period -> higher priority):

- Utilization per task:
  - U_i = C_i / T_i
- Total utilization:
  - U = sum(U_i)
- Liu-Layland sufficient bound:
  - U <= n * (2^(1/n) - 1)

Use response-time analysis (RTA) for exact schedulability under fixed priorities:

- For task i:
  - R_i^(0) = C_i
  - R_i^(k+1) = C_i + sum over higher-priority j of ceil(R_i^(k) / T_j) * C_j
- Stop when:
  - converged (R_i^(k+1) = R_i^(k)), then compare R_i <= D_i
  - or R_i > D_i (unschedulable)

For implicit deadlines, D_i = T_i.

## 5) Safety margins (practical guidance)

- Keep worst-case response-time margin:
  - (D_i - R_i) / D_i >= 20% target
- Keep inferred utilization headroom:
  - U_total <= 0.7 preferred for embedded systems with ISR and integration noise.
- Re-run campaign after any change to:
  - compiler flags, clocking, model, RTOS tick, task priorities, logging level.

## 6) Host pipeline usage

1. Capture UART to file:

```bash
python analysis/capture_uart.py --port COM5 --baud 115200 --out wcet_run1.log --duration-s 900
```

2. Generate WCET + RM summary using inferred inference period:

```bash
python analysis/wcet_rm_report.py --log wcet_run1.log --cpu-hz 50000000 --ci bound
```

3. Optional: full multi-task RTA by supplying task set CSV:

```csv
task,priority,period_ms,deadline_ms,ci_ms,ci_cycles,ci_source
Sensor,1,20,20,0.200,,scope
  Inference,2,100,100,,,from_wcet
UART,3,10,10,0.080,,scope
Logger,4,1000,1000,0.300,,scope
```

Then:

```bash
python analysis/wcet_rm_report.py --log wcet_run1.log --task-csv task_set.csv --ci bound
```

If `Inference` row omits `ci_ms` and `ci_cycles`, the tool injects C_i from WCET according to `--ci`.

4. Auto-build task-set CSV from captured task timings:

```bash
python analysis/build_task_set_from_log.py --log wcet_run1.log --out task_set_measured.csv
python analysis/wcet_rm_report.py --log wcet_run1.log --task-csv task_set_measured.csv --ci bound
```

If the builder reports `UART ci_ms exceeds uart period`, your log likely captured
replay/streaming behavior. For strict periodic RM modeling, disable replay and rerun.
Use `--allow-uart-overrun` only when intentionally modeling that heavier mode.
