"""
UCI HAR Pipeline — Week 1
--------------------------
1. Download UCI HAR dataset
2. Train 2-layer MLP with sklearn
3. Quantize weights to INT8
4. Export weights + inference code as C header

Usage:
    python har_pipeline.py

Outputs:
    har_model.h   — C header with INT8 weights + fixed-point inference
    model_stats.txt — accuracy, layer shapes, quantization error
"""

import os
import io
import zipfile
import struct
import textwrap
import urllib.request
import numpy as np
from sklearn.neural_network import MLPClassifier
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import accuracy_score, classification_report

# ──────────────────────────────────────────────
# CONFIG
# ──────────────────────────────────────────────
DATASET_URL = "https://archive.ics.uci.edu/ml/machine-learning-databases/00240/UCI%20HAR%20Dataset.zip"
DATA_DIR    = "./uci_har_data"
HEADER_OUT  = "./har_model.h"
STATS_OUT   = "./model_stats.txt"
MIF_OUT     = "./weights_hex.mif"

# MLP architecture
HIDDEN_LAYER_1 = 64
HIDDEN_LAYER_2 = 32

# INT8 quantization scale (symmetric, per-layer)
INT8_MAX = 127

# Activity labels (1-indexed in dataset)
ACTIVITY_LABELS = {
    1: "WALKING",
    2: "WALKING_UPSTAIRS",
    3: "WALKING_DOWNSTAIRS",
    4: "SITTING",
    5: "STANDING",
    6: "LAYING",
}

# ──────────────────────────────────────────────
# STEP 1 — DOWNLOAD & EXTRACT
# ──────────────────────────────────────────────
def download_dataset():
    if os.path.exists(os.path.join(DATA_DIR, "UCI HAR Dataset")):
        print("[1/4] Dataset already downloaded, skipping.")
        return

    print("[1/4] Downloading UCI HAR dataset (~60 MB)...")
    os.makedirs(DATA_DIR, exist_ok=True)

    zip_path = os.path.join(DATA_DIR, "uci_har.zip")
    urllib.request.urlretrieve(DATASET_URL, zip_path)

    print("      Extracting...")
    with zipfile.ZipFile(zip_path, "r") as z:
        z.extractall(DATA_DIR)
    os.remove(zip_path)
    print("      Done.")


def load_split(split):
    """Load X (561 features) and y (labels) for 'train' or 'test'."""
    base = os.path.join(DATA_DIR, "UCI HAR Dataset", split)
    X = np.loadtxt(os.path.join(base, f"X_{split}.txt"))
    y = np.loadtxt(os.path.join(base, f"y_{split}.txt"), dtype=int)
    return X, y


# ──────────────────────────────────────────────
# STEP 2 — TRAIN MLP
# ──────────────────────────────────────────────
def train_model(X_train, y_train, X_test, y_test):
    print("[2/4] Training 2-layer MLP (561 → 64 → 32 → 6)...")

    scaler = StandardScaler()
    X_train_s = scaler.fit_transform(X_train)
    X_test_s  = scaler.transform(X_test)

    mlp = MLPClassifier(
        hidden_layer_sizes=(HIDDEN_LAYER_1, HIDDEN_LAYER_2),
        activation="relu",
        solver="adam",
        max_iter=200,
        random_state=42,
        verbose=False,
        early_stopping=True,
        n_iter_no_change=10,
    )
    mlp.fit(X_train_s, y_train)

    y_pred = mlp.predict(X_test_s)
    acc = accuracy_score(y_test, y_pred)
    report = classification_report(
        y_test, y_pred,
        target_names=[ACTIVITY_LABELS[i] for i in range(1, 7)]
    )
    print(f"      Test accuracy: {acc*100:.2f}%")

    return mlp, scaler, acc, report


# ──────────────────────────────────────────────
# STEP 3 — INT8 QUANTIZATION
# ──────────────────────────────────────────────
def quantize_layer(W, b):
    """
    Symmetric per-tensor INT8 quantization.
    Returns:
        W_q, b_q  — int8 numpy arrays
        w_scale   — float32 scale for weights
        b_scale   — float32 scale for biases
    """
    w_max   = np.max(np.abs(W))
    w_scale = w_max / INT8_MAX if w_max != 0 else 1.0
    W_q     = np.clip(np.round(W / w_scale), -INT8_MAX, INT8_MAX).astype(np.int8)

    b_max   = np.max(np.abs(b))
    b_scale = b_max / INT8_MAX if b_max != 0 else 1.0
    b_q     = np.clip(np.round(b / b_scale), -INT8_MAX, INT8_MAX).astype(np.int8)

    return W_q, b_q, w_scale, b_scale


def quantize_scaler(scaler, n_features):
    """Quantize StandardScaler mean and 1/std for fixed-point preprocessing."""
    mean    = scaler.mean_.astype(np.float32)
    inv_std = (1.0 / scaler.scale_).astype(np.float32)
    return mean, inv_std


def quantization_error(W, W_q, scale):
    W_dq = W_q.astype(np.float32) * scale
    return np.mean(np.abs(W - W_dq))


def quantize_model(mlp):
    print("[3/4] Quantizing weights to INT8...")
    layers = []
    errors = []
    for i, (W, b) in enumerate(zip(mlp.coefs_, mlp.intercepts_)):
        W_q, b_q, w_scale, b_scale = quantize_layer(W, b)
        err = quantization_error(W, W_q, w_scale)
        errors.append(err)
        layers.append({
            "index":   i,
            "W":       W,
            "b":       b,
            "W_q":     W_q,
            "b_q":     b_q,
            "w_scale": w_scale,
            "b_scale": b_scale,
            "in_dim":  W.shape[0],
            "out_dim": W.shape[1],
        })
        print(f"      Layer {i}: {W.shape[0]}×{W.shape[1]}  "
              f"w_scale={w_scale:.6f}  mean_abs_err={err:.6f}")
    return layers, errors


# ──────────────────────────────────────────────
# STEP 4 — EXPORT C HEADER
# ──────────────────────────────────────────────
def array_to_c(name, arr, ctype="int8_t", cols=16):
    """Format a numpy array as a C array literal."""
    flat  = arr.flatten()
    total = len(flat)
    lines = []
    for i in range(0, total, cols):
        chunk = flat[i:i+cols]
        lines.append("    " + ", ".join(f"{int(v):4d}" for v in chunk) + ",")
    body = "\n".join(lines).rstrip(",")
    return f"static const {ctype} {name}[{total}] = {{\n{body}\n}};"


def float_array_to_c(name, arr, cols=8):
    flat  = arr.flatten()
    total = len(flat)
    lines = []
    for i in range(0, total, cols):
        chunk = flat[i:i+cols]
        lines.append("    " + ", ".join(f"{v:.8f}f" for v in chunk) + ",")
    body = "\n".join(lines).rstrip(",")
    return f"static const float {name}[{total}] = {{\n{body}\n}};"


def export_header(layers, scaler, n_features, n_classes, header_path):
    print(f"[4/4] Exporting C header → {header_path}")

    mean, inv_std = quantize_scaler(scaler, n_features)

    guard = "HAR_MODEL_H"
    lines = []

    # ── Header guard & includes ──
    lines += [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "/*",
        " * har_model.h — Auto-generated by har_pipeline.py",
        " * UCI HAR 2-layer MLP, INT8 quantized weights",
        " *",
        " * Architecture: 561 -> 64 (ReLU) -> 32 (ReLU) -> 6 (softmax)",
        " * Inference: fixed-point INT8 MAC, dequantize per layer",
        " *",
        " * Usage:",
        " *   float input[HAR_INPUT_DIM];   // pre-filled with raw sensor features",
        " *   int label = har_infer(input); // returns 0..5",
        " */",
        "",
        "#include <stdint.h>",
        "#include <math.h>",   # for expf in softmax
        "",
    ]

    # ── Dimensions ──
    lines += [
        "/* ── Dimensions ─────────────────────────────── */",
        f"#define HAR_INPUT_DIM    {n_features}",
        f"#define HAR_HIDDEN1_DIM  {layers[0]['out_dim']}",
        f"#define HAR_HIDDEN2_DIM  {layers[1]['out_dim']}",
        f"#define HAR_OUTPUT_DIM   {n_classes}",
        f"#define HAR_NUM_LAYERS   {len(layers)}",
        "",
    ]

    # ── Activity label strings ──
    label_list = ", ".join(f'"{ACTIVITY_LABELS[i]}"' for i in range(1, 7))
    lines += [
        "/* ── Activity labels ────────────────────────── */",
        f"static const char* HAR_LABELS[{n_classes}] = {{ {label_list} }};",
        "",
    ]

    # ── Scaler: mean and inv_std ──
    lines += [
        "/* ── StandardScaler parameters ──────────────── */",
        float_array_to_c("har_scaler_mean",    mean),
        "",
        float_array_to_c("har_scaler_inv_std", inv_std),
        "",
    ]

    # ── Weights and biases per layer ──
    lines.append("/* ── Quantized weights & biases ─────────────── */")
    for L in layers:
        i = L["index"]
        lines += [
            f"/* Layer {i}: {L['in_dim']}×{L['out_dim']} */",
            f"#define HAR_L{i}_W_SCALE  {L['w_scale']:.8f}f",
            f"#define HAR_L{i}_B_SCALE  {L['b_scale']:.8f}f",
            "",
            array_to_c(f"har_W{i}", L["W_q"]),
            "",
            array_to_c(f"har_b{i}", L["b_q"]),
            "",
        ]

    # ── Inline inference function ──
    # Written in plain C89-compatible style for TM4C
    inference_code = textwrap.dedent(f"""\
        /* ── Inference ───────────────────────────────────
         * har_infer(float *x)
         *   x      : raw feature vector, length HAR_INPUT_DIM
         *   returns: predicted class index 0..{n_classes-1}
         *
         * Stack usage (worst case):
         *   {n_features}*4 + {HIDDEN_LAYER_1}*4 + {HIDDEN_LAYER_2}*4 + {n_classes}*4 = ~{(n_features+HIDDEN_LAYER_1+HIDDEN_LAYER_2+n_classes)*4} bytes
         *   For TM4C (8 KB default stack) this is fine.
         */
        static inline int har_infer(const float *x)
        {{
            int i, j;
            float scaled[HAR_INPUT_DIM];
            float h1[HAR_HIDDEN1_DIM];
            float h2[HAR_HIDDEN2_DIM];
            float out[HAR_OUTPUT_DIM];
            float acc, max_val, sum_exp;
            int   best;

            /* --- Standardize input --- */
            for (i = 0; i < HAR_INPUT_DIM; i++) {{
                scaled[i] = (x[i] - har_scaler_mean[i]) * har_scaler_inv_std[i];
            }}

            /* --- Layer 0: 561 -> 64, ReLU --- */
            for (j = 0; j < HAR_HIDDEN1_DIM; j++) {{
                acc = (float)har_b0[j] * HAR_L0_B_SCALE;
                for (i = 0; i < HAR_INPUT_DIM; i++) {{
                    acc += scaled[i] * ((float)har_W0[i * HAR_HIDDEN1_DIM + j] * HAR_L0_W_SCALE);
                }}
                h1[j] = acc > 0.0f ? acc : 0.0f;  /* ReLU */
            }}

            /* --- Layer 1: 64 -> 32, ReLU --- */
            for (j = 0; j < HAR_HIDDEN2_DIM; j++) {{
                acc = (float)har_b1[j] * HAR_L1_B_SCALE;
                for (i = 0; i < HAR_HIDDEN1_DIM; i++) {{
                    acc += h1[i] * ((float)har_W1[i * HAR_HIDDEN2_DIM + j] * HAR_L1_W_SCALE);
                }}
                h2[j] = acc > 0.0f ? acc : 0.0f;  /* ReLU */
            }}

            /* --- Layer 2: 32 -> 6, linear --- */
            for (j = 0; j < HAR_OUTPUT_DIM; j++) {{
                acc = (float)har_b2[j] * HAR_L2_B_SCALE;
                for (i = 0; i < HAR_HIDDEN2_DIM; i++) {{
                    acc += h2[i] * ((float)har_W2[i * HAR_OUTPUT_DIM + j] * HAR_L2_W_SCALE);
                }}
                out[j] = acc;
            }}

            /* --- Softmax argmax (no need for full softmax to get label) --- */
            best = 0;
            max_val = out[0];
            for (j = 1; j < HAR_OUTPUT_DIM; j++) {{
                if (out[j] > max_val) {{ max_val = out[j]; best = j; }}
            }}

            return best;  /* 0-indexed; add 1 for UCI label, or use HAR_LABELS[best] */
        }}
    """)

    lines += [
        inference_code,
        f"#endif /* {guard} */",
        "",
    ]

    with open(header_path, "w") as f:
        f.write("\n".join(lines))

    size_kb = os.path.getsize(header_path) / 1024
    print(f"      Written {size_kb:.1f} KB")


# ──────────────────────────────────────────────
# STEP 5 — EXPORT .MIF (FPGA BRAM)
# ──────────────────────────────────────────────
def export_mif(layers, scaler, mif_path):
    """
    Write an Altera/Intel Memory Initialization File (.mif) containing
    all INT8 weights and biases, followed by the float32 scaler params.

    Memory layout (byte-addressable, 8-bit width):
      [0 .. W0_size-1]            Layer 0 weights  (INT8, row-major)
      [W0_size .. +b0_size-1]     Layer 0 biases   (INT8)
      [.. W1, b1, W2, b2 ..]     Layers 1 & 2     (INT8)
      [after biases]              Scaler mean       (float32, 4 bytes each)
      [after mean]                Scaler inv_std    (float32, 4 bytes each)

    The FPGA inference core reads this block from BRAM at startup and
    reconstructs the weight matrices. Scales (w_scale, b_scale) are
    burned into the RTL as parameters — they don't live in BRAM.

    Width  = 8 bits  (one byte per address)
    Depth  = total number of bytes
    """
    import struct

    # ── Collect all bytes in layout order ──
    all_bytes = bytearray()

    section_map = []   # (label, start_addr, byte_count) for the header comment

    for L in layers:
        # Weights: stored row-major (same order as numpy default)
        w_bytes = L["W_q"].flatten().tobytes()
        section_map.append((f"L{L['index']} weights ({L['in_dim']}x{L['out_dim']})",
                            len(all_bytes), len(w_bytes)))
        all_bytes += w_bytes

        b_bytes = L["b_q"].flatten().tobytes()
        section_map.append((f"L{L['index']} biases  ({L['out_dim']})",
                            len(all_bytes), len(b_bytes)))
        all_bytes += b_bytes

    # Scaler mean — float32 little-endian
    mean_bytes = scaler.mean_.astype(np.float32).tobytes()
    section_map.append(("Scaler mean    (561 x float32)", len(all_bytes), len(mean_bytes)))
    all_bytes += mean_bytes

    # Scaler inv_std — float32 little-endian
    inv_std_bytes = (1.0 / scaler.scale_).astype(np.float32).tobytes()
    section_map.append(("Scaler inv_std (561 x float32)", len(all_bytes), len(inv_std_bytes)))
    all_bytes += inv_std_bytes

    depth = len(all_bytes)
    addr_width = len(f"{depth-1:X}")   # hex digits needed for addresses

    # ── Write .mif ──
    with open(mif_path, "w") as f:
        f.write("-- HAR MLP weights — auto-generated by har_pipeline.py\n")
        f.write("-- DO NOT EDIT BY HAND\n")
        f.write("--\n")
        f.write("-- Memory layout:\n")
        for label, start, count in section_map:
            f.write(f"--   0x{start:06X} .. 0x{start+count-1:06X}  {label}\n")
        f.write("--\n")
        f.write(f"WIDTH = 8;\n")
        f.write(f"DEPTH = {depth};\n")
        f.write("ADDRESS_RADIX = HEX;\n")
        f.write("DATA_RADIX = HEX;\n")
        f.write("\n")
        f.write("CONTENT BEGIN\n")

        # Write run-length encoded ranges where possible to keep file size sane
        i = 0
        while i < depth:
            b = all_bytes[i]
            # Count consecutive identical bytes
            run = 1
            while i + run < depth and all_bytes[i + run] == b and run < 256:
                run += 1
            addr_str = f"{i:0{addr_width}X}"
            if run > 1:
                f.write(f"  [{i:0{addr_width}X}..{i+run-1:0{addr_width}X}]  :  {b:02X};\n")
            else:
                f.write(f"  {addr_str}  :  {b:02X};\n")
            i += run

        f.write("END;\n")

    size_kb = os.path.getsize(mif_path) / 1024
    raw_kb  = depth / 1024
    print(f"      Written {size_kb:.1f} KB  ({raw_kb:.1f} KB raw data, {depth:,} addresses)")
    print(f"      Depth={depth}  Width=8  — use these in your Quartus BRAM IP")
    return depth


# ──────────────────────────────────────────────
# STEP 6 — SAVE STATS
# ──────────────────────────────────────────────
def save_stats(acc, report, layers, errors, stats_path):
    lines = [
        "UCI HAR MLP — Model Stats",
        "=" * 40,
        f"Test accuracy (float32): {acc*100:.2f}%",
        "",
        "Classification report:",
        report,
        "",
        "Quantization summary (INT8, symmetric per-tensor):",
    ]
    for L, err in zip(layers, errors):
        lines.append(
            f"  Layer {L['index']} ({L['in_dim']}x{L['out_dim']}): "
            f"w_scale={L['w_scale']:.6f}  mean_abs_err={err:.6f}"
        )
    lines += [
        "",
        "Weight array sizes (INT8):",
    ]
    total_bytes = 0
    for L in layers:
        wb = L["W_q"].size + L["b_q"].size
        total_bytes += wb
        lines.append(f"  Layer {L['index']}: {wb:,} bytes")
    lines.append(f"  TOTAL: {total_bytes:,} bytes ({total_bytes/1024:.1f} KB)")

    with open(stats_path, "w") as f:
        f.write("\n".join(lines))
    print(f"      Stats → {stats_path}")


# ──────────────────────────────────────────────
# MAIN
# ──────────────────────────────────────────────
if __name__ == "__main__":
    # 1. Download
    download_dataset()

    # 2. Load
    X_train, y_train = load_split("train")
    X_test,  y_test  = load_split("test")
    n_features = X_train.shape[1]   # 561
    n_classes  = len(ACTIVITY_LABELS)  # 6
    print(f"      Train: {X_train.shape}, Test: {X_test.shape}")

    # 3. Train
    mlp, scaler, acc, report = train_model(X_train, y_train, X_test, y_test)

    # 4. Quantize
    layers, errors = quantize_model(mlp)

    # 5. Export header
    export_header(layers, scaler, n_features, n_classes, HEADER_OUT)

    # 5. Export .mif for FPGA BRAM
    print("[5/5] Exporting FPGA BRAM initialisation file...")
    bram_depth = export_mif(layers, scaler, MIF_OUT)

    # 6. Save stats
    save_stats(acc, report, layers, errors, STATS_OUT)

    print("\n✓ Pipeline complete.")
    print(f"  → {HEADER_OUT}   (TM4C: #include this)")
    print(f"  → {MIF_OUT}      (FPGA: load into Quartus BRAM IP, Depth={bram_depth} Width=8)")
    print(f"  → {STATS_OUT}")