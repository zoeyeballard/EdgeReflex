"""
replay.py — UART IMU Data Replay Script
-----------------------------------------
Streams pre-recorded UCI HAR feature frames to the TM4C over USB serial,
one frame at a time, and prints the TM4C's inference response alongside
ground truth so you can validate the end-to-end pipeline.

Packet format (laptop → TM4C):
  [0x00] 0xAA          sync byte
  [0x01] seq           frame sequence number (0–255, wraps)
  [0x02..0x232] data   561 x INT8 quantized features (row from X_test)
  [0x233] label        ground truth class (1–6, UCI convention)
  [0x234] crc8         CRC-8/MAXIM over bytes [0x01..0x233]

Total: 564 bytes per frame.

Response format (TM4C → laptop), one line:
  PRED:<LABEL> CYCLES:<N> GT:<LABEL>\r\n
  e.g. "PRED:WALKING CYCLES:14823 GT:WALKING\r\n"

Usage examples:
  # Auto-detect port, stream first 100 frames at 1 frame/sec
  python replay.py

  # Specific port, all frames, 10 frames/sec, loop when done
  python replay.py --port COM3 --baud 115200 --fps 10 --loop

  # Dry run (no serial port needed) — prints what would be sent
  python replay.py --dry-run --frames 20

  # Specify data directory if UCI HAR is not in default location
  python replay.py --data ../training/uci_har_data

Arguments:
  --port      Serial port (e.g. COM3, /dev/ttyACM0). Auto-detected if omitted.
  --baud      Baud rate. Default: 115200.
  --fps       Frames per second to send. Default: 1.0.
  --frames    Number of frames to send. Default: 100. 0 = all.
  --loop      Loop the dataset when exhausted.
  --dry-run   Run without a serial port. Prints frames to console.
  --data      Path to uci_har_data directory. Default: ./uci_har_data.
  --scaler    Path to scaler .npz (auto-saved on first run). Default: ./scaler.npz.
  --verbose   Print every frame. Default: print summary every 10 frames.
"""

import argparse
import sys
import os
import time
import struct
import numpy as np

# ──────────────────────────────────────────────
# CONSTANTS
# ──────────────────────────────────────────────
SYNC_BYTE   = 0xAA
FRAME_SIZE  = 564          # sync(1) + seq(1) + features(561) + label(1) + crc(1)
RESP_TIMEOUT = 2.0         # seconds to wait for TM4C response per frame
INT8_MAX    = 127

ACTIVITY_LABELS = {
    1: "WALKING",
    2: "WALKING_UPSTAIRS",
    3: "WALKING_DOWNSTAIRS",
    4: "SITTING",
    5: "STANDING",
    6: "LAYING",
}

# ──────────────────────────────────────────────
# CRC-8/MAXIM  (polynomial 0x31, init 0x00)
# Used by many sensor ICs — easy to implement on TM4C
# ──────────────────────────────────────────────
def crc8_maxim(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x31) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


# ──────────────────────────────────────────────
# DATA LOADING
# ──────────────────────────────────────────────
def load_test_data(data_dir: str, scaler_path: str):
    """
    Load X_test and y_test from UCI HAR, apply StandardScaler,
    and quantize features to INT8.

    The scaler parameters come from scaler.npz which is saved by
    har_pipeline.py on its first run. If not found, we fit a new
    scaler on X_train — same result, just takes a few seconds.
    """
    har_dir = os.path.join(data_dir, "UCI HAR Dataset")
    if not os.path.exists(har_dir):
        print(f"[error] UCI HAR dataset not found at: {har_dir}")
        print("        Run har_pipeline.py first to download it.")
        sys.exit(1)

    print("[data] Loading X_test / y_test ...")
    X_test = np.loadtxt(os.path.join(har_dir, "test", "X_test.txt"))
    y_test = np.loadtxt(os.path.join(har_dir, "test", "y_test.txt"), dtype=int)

    # Load or fit scaler
    if os.path.exists(scaler_path):
        npz = np.load(scaler_path)
        mean    = npz["mean"]
        inv_std = npz["inv_std"]
        print(f"[data] Loaded scaler from {scaler_path}")
    else:
        print("[data] scaler.npz not found — fitting from X_train ...")
        X_train = np.loadtxt(os.path.join(har_dir, "train", "X_train.txt"))
        mean    = X_train.mean(axis=0).astype(np.float32)
        std     = X_train.std(axis=0).astype(np.float32)
        inv_std = (1.0 / np.where(std == 0, 1.0, std)).astype(np.float32)
        np.savez(scaler_path, mean=mean, inv_std=inv_std)
        print(f"[data] Saved scaler to {scaler_path}")

    # Standardize
    X_scaled = ((X_test - mean) * inv_std).astype(np.float32)

    # Quantize to INT8
    w_max   = np.max(np.abs(X_scaled), axis=0)
    w_max   = np.where(w_max == 0, 1.0, w_max)
    scale   = w_max / INT8_MAX
    X_int8  = np.clip(np.round(X_scaled / scale), -INT8_MAX, INT8_MAX).astype(np.int8)

    print(f"[data] {X_int8.shape[0]} test frames ready  "
          f"({X_int8.shape[1]} features each, INT8)")
    return X_int8, y_test


# ──────────────────────────────────────────────
# FRAME BUILDING
# ──────────────────────────────────────────────
def build_frame(seq: int, features: np.ndarray, label: int) -> bytes:
    """
    Pack one 564-byte frame.
    CRC covers: seq(1) + features(561) + label(1) = 563 bytes.
    """
    seq_byte    = seq & 0xFF
    feat_bytes  = features.tobytes()          # 561 bytes, signed
    label_byte  = label & 0xFF

    payload = bytes([seq_byte]) + feat_bytes + bytes([label_byte])
    crc     = crc8_maxim(payload)

    return bytes([SYNC_BYTE]) + payload + bytes([crc])


# ──────────────────────────────────────────────
# PORT AUTO-DETECTION
# ──────────────────────────────────────────────
def find_port() -> str | None:
    try:
        import serial.tools.list_ports
        candidates = []
        for p in serial.tools.list_ports.comports():
            desc = (p.description or "").lower()
            # TM4C Launchpad shows up as XDS110 or Stellaris Virtual Serial
            if any(k in desc for k in ["xds110", "stellaris", "launchpad",
                                        "tiva", "uart", "usb serial"]):
                candidates.append(p.device)
        if candidates:
            return candidates[0]
        # Fall back to first available port
        ports = list(serial.tools.list_ports.comports())
        return ports[0].device if ports else None
    except Exception:
        return None


# ──────────────────────────────────────────────
# STATS TRACKER
# ──────────────────────────────────────────────
class Stats:
    def __init__(self):
        self.sent       = 0
        self.correct    = 0
        self.timeouts   = 0
        self.crc_errors = 0
        self.cycles_list = []

    def update(self, pred_label: str | None, gt_label: str, cycles: int | None):
        self.sent += 1
        if pred_label == gt_label:
            self.correct += 1
        if pred_label is None:
            self.timeouts += 1
        if cycles is not None:
            self.cycles_list.append(cycles)

    @property
    def accuracy(self):
        if self.sent == 0:
            return 0.0
        return self.correct / self.sent * 100

    @property
    def avg_cycles(self):
        if not self.cycles_list:
            return 0
        return int(np.mean(self.cycles_list))

    @property
    def avg_ms(self):
        # TM4C runs at 80 MHz by default
        return self.avg_cycles / 80_000

    def summary_line(self):
        return (f"frames={self.sent}  acc={self.accuracy:.1f}%  "
                f"timeouts={self.timeouts}  "
                f"avg_cycles={self.avg_cycles}  "
                f"avg_ms={self.avg_ms:.2f}")


# ──────────────────────────────────────────────
# RESPONSE PARSING
# ──────────────────────────────────────────────
def parse_response(line: str) -> tuple[str | None, int | None]:
    """
    Parse: "PRED:WALKING CYCLES:14823 GT:WALKING\r\n"
    Returns (pred_label, cycles). Either may be None on parse failure.
    """
    pred   = None
    cycles = None
    try:
        for token in line.strip().split():
            if token.startswith("PRED:"):
                pred = token[5:]
            elif token.startswith("CYCLES:"):
                cycles = int(token[7:])
    except Exception:
        pass
    return pred, cycles


# ──────────────────────────────────────────────
# DRY-RUN MODE
# ──────────────────────────────────────────────
def run_dry(X_int8, y_test, args):
    """Print frame info without opening any serial port."""
    print("\n[dry-run] No serial port — printing frame summaries only\n")
    stats    = Stats()
    n_frames = len(X_int8) if args.frames == 0 else min(args.frames, len(X_int8))
    interval = 1.0 / args.fps

    for i in range(n_frames):
        idx     = i % len(X_int8)
        seq     = i & 0xFF
        label   = int(y_test[idx])
        frame   = build_frame(seq, X_int8[idx], label)
        gt_name = ACTIVITY_LABELS.get(label, f"CLASS_{label}")

        # Verify CRC round-trips correctly
        recomputed = crc8_maxim(frame[1:-1])
        crc_ok     = (recomputed == frame[-1])

        print(f"  frame #{i:04d}  seq={seq:3d}  gt={gt_name:<20s}  "
              f"bytes={len(frame)}  crc={'OK' if crc_ok else 'FAIL'}")

        # Simulate a plausible TM4C response for display
        stats.update(gt_name, gt_name, 14800 + np.random.randint(-200, 200))

        if (i + 1) % 10 == 0:
            print(f"  --- {stats.summary_line()}")

        time.sleep(interval)

    print(f"\n[dry-run] Done.  {stats.summary_line()}")


# ──────────────────────────────────────────────
# LIVE SERIAL MODE
# ──────────────────────────────────────────────
def run_serial(X_int8, y_test, args):
    import serial

    port = args.port or find_port()
    if port is None:
        print("[error] No serial port found. Use --port to specify one,")
        print("        or --dry-run to test without hardware.")
        sys.exit(1)

    print(f"\n[serial] Opening {port} @ {args.baud} baud ...")
    try:
        ser = serial.Serial(port, baudrate=args.baud, timeout=RESP_TIMEOUT)
    except serial.SerialException as e:
        print(f"[error] Could not open port: {e}")
        sys.exit(1)

    # Flush any stale data
    time.sleep(0.1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print(f"[serial] Connected. Starting stream ...\n")

    stats    = Stats()
    interval = 1.0 / args.fps
    total    = len(X_int8)
    n_frames = total if args.frames == 0 else args.frames
    i        = 0

    try:
        while i < n_frames:
            idx      = i % total
            seq      = i & 0xFF
            label    = int(y_test[idx])
            gt_name  = ACTIVITY_LABELS.get(label, f"CLASS_{label}")
            frame    = build_frame(seq, X_int8[idx], label)

            # ── Send frame ──
            t0 = time.perf_counter()
            ser.write(frame)
            ser.flush()

            # ── Read response ──
            raw = ser.readline()
            elapsed = time.perf_counter() - t0

            if not raw:
                pred_name, cycles = None, None
                result_tag = "TIMEOUT"
                stats.timeouts += 1
                stats.sent += 1
            else:
                line = raw.decode("ascii", errors="replace")
                pred_name, cycles = parse_response(line)
                correct    = (pred_name == gt_name)
                result_tag = "OK   " if correct else "WRONG"
                stats.update(pred_name, gt_name, cycles)

            # ── Print ──
            if args.verbose or (i % 10 == 0):
                cyc_str = f"CYCLES:{cycles}" if cycles else "CYCLES:---"
                print(f"  [#{i:04d}] sent {gt_name:<20s} → "
                      f"PRED:{pred_name or '?':<20s}  "
                      f"{cyc_str:<16s}  {result_tag}  "
                      f"rtt={elapsed*1000:.1f}ms")

            if (i + 1) % 10 == 0 and not args.verbose:
                print(f"  --- {stats.summary_line()}")

            i += 1

            # Loop dataset if requested
            if args.loop and i >= n_frames:
                i = 0
                print("[replay] Looping dataset ...")

            # Pace to requested fps
            sleep_time = interval - (time.perf_counter() - t0)
            if sleep_time > 0:
                time.sleep(sleep_time)

    except KeyboardInterrupt:
        print("\n[replay] Interrupted by user.")

    finally:
        ser.close()
        print(f"\n[serial] Port closed.")

    print(f"\n[done] {stats.summary_line()}")
    print(f"       TM4C running at 80 MHz → avg latency {stats.avg_ms:.2f} ms/frame")


# ──────────────────────────────────────────────
# ENTRY POINT
# ──────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Replay UCI HAR frames to TM4C over UART and log inference results."
    )
    parser.add_argument("--port",    default=None,
                        help="Serial port, e.g. COM3 or /dev/ttyACM0")
    parser.add_argument("--baud",    type=int,   default=115200)
    parser.add_argument("--fps",     type=float, default=1.0,
                        help="Frames per second (default 1.0)")
    parser.add_argument("--frames",  type=int,   default=100,
                        help="Frames to send (0 = all)")
    parser.add_argument("--loop",    action="store_true",
                        help="Loop dataset when exhausted")
    parser.add_argument("--dry-run", action="store_true",
                        help="Run without serial port — prints frame summaries")
    parser.add_argument("--data",    default="../training/uci_har_data",
                        help="Path to uci_har_data directory")
    parser.add_argument("--scaler",  default="../training/scaler.npz",
                        help="Path to scaler .npz file")
    parser.add_argument("--verbose", action="store_true",
                        help="Print every frame (default: every 10)")
    args = parser.parse_args()

    print("=" * 60)
    print("  HAR UART Replay")
    print(f"  source : {args.data}")
    print(f"  fps    : {args.fps}")
    print(f"  frames : {'all' if args.frames == 0 else args.frames}")
    port_str = args.port or "auto-detect"
    mode_str = "dry-run" if args.dry_run else f"serial ({port_str})"
    print(f"  mode   : {mode_str}")
    print("=" * 60)

    X_int8, y_test = load_test_data(args.data, args.scaler)

    if args.dry_run:
        run_dry(X_int8, y_test, args)
    else:
        run_serial(X_int8, y_test, args)


if __name__ == "__main__":
    main()