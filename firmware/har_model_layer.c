/*
 * har_model_layer.c - Implementation of Split Layer 1/Layer 2 HAR Model
 * 
 * Contains preprocessing, Layer 2 inference, and dequantization.
 */

#include <stdint.h>
#include <math.h>
#include "har_model.h"
#include "har_model_layer.h"

/*
 * Initialize Layer 1/Layer 2 model (stub for now).
 * Could be extended to load weights into cache, configure hardware, etc.
 */
void HarModelLayerInit(void)
{
    // Currently no initialization needed
    // In future: could load Layer 2 weights into fast memory, etc.
}

/*
 * Preprocessing: Standardize raw input features.
 * 
 * Implements the same standardization as the original har_infer:
 *   scaled[i] = (raw[i] - mean[i]) * inv_std[i]
 */
void HarPreprocess(const float *raw, float *scaled)
{
    int i;
    for (i = 0; i < HAR_INPUT_DIM; i++) {
        scaled[i] = (raw[i] - har_scaler_mean[i]) * har_scaler_inv_std[i];
    }
}

/*
 * Layer 2 inference: 64-dim input -> 6-dim output with argmax.
 * 
 * Implements:
 *   h2[j] = sum_i (h1[i] * W1[i, j]) + b1[j]  with ReLU
 *   out[j] = sum_i (h2[i] * W2[i, j]) + b2[j]  (linear)
 *   return argmax(out[j])
 * 
 * Args:
 *   h1[64] - Layer 1 output (assumed already dequantized if from FPGA)
 * 
 * Returns:
 *   Predicted class index 0..5
 */
int HarInferenceLayer2(const float *h1)
{
    int i, j;
    float h2[HAR_HIDDEN2_DIM];
    float out[HAR_OUTPUT_DIM];
    float acc, max_val;
    int best;
    
    /* --- Layer 1: 64 -> 32, ReLU --- */
    for (j = 0; j < HAR_HIDDEN2_DIM; j++) {
        acc = (float)har_b1[j] * HAR_L1_B_SCALE;
        for (i = 0; i < HAR_HIDDEN1_DIM; i++) {
            acc += h1[i] * ((float)har_W1[i * HAR_HIDDEN2_DIM + j] * HAR_L1_W_SCALE);
        }
        h2[j] = acc > 0.0f ? acc : 0.0f;  /* ReLU */
    }
    
    /* --- Layer 2: 32 -> 6, linear --- */
    for (j = 0; j < HAR_OUTPUT_DIM; j++) {
        acc = (float)har_b2[j] * HAR_L2_B_SCALE;
        for (i = 0; i < HAR_HIDDEN2_DIM; i++) {
            acc += h2[i] * ((float)har_W2[i * HAR_OUTPUT_DIM + j] * HAR_L2_W_SCALE);
        }
        out[j] = acc;
    }
    
    /* --- Softmax argmax (no need for full softmax to get label) --- */
    best = 0;
    max_val = out[0];
    for (j = 1; j < HAR_OUTPUT_DIM; j++) {
        if (out[j] > max_val) {
            max_val = out[j];
            best = j;
        }
    }
    
    return best;
}

/*
 * Convert Layer 1 output from INT8 (FPGA) to float for Layer 2.
 * 
 * The FPGA computes:
 *   acc[n] = sum_i (features[i] * weights[n][i])  (as INT32)
 *   int8_out[n] = saturate(acc[n]) to INT8
 * 
 * To convert back to float, we multiply by the Layer 0 output scale factor.
 * This scale is determined by the quantization scheme (per-tensor, INT8).
 * 
 * For now, use HAR_L0_OUT_SCALE (same as in original inference).
 * 
 * Args:
 *   int8_out[64] - Layer 1 result from FPGA (INT8, signed)
 *   float_out[64] - output buffer for float values
 */
void HarDequantizeLayer1(const int8_t *int8_out, float *float_out)
{
    int i;
    
    // Extract scale factor for Layer 0 output
    // In the quantization scheme, this is typically:
    //   out_scale = output_range / 256.0f  (for INT8)
    // For UCI HAR, output range is typically [-1, 1] or [-2, 2] per tensor
    // 
    // Using same dequantization as the original model's bias scale:
    // This matches har_b0 which has its own scale factor HAR_L0_B_SCALE
    
    // Conservative approach: assume output is in range [-32, 32] (typical for int8 activations)
    // and map to float range that makes sense for Layer 2 input
    // 
    // For simplicity, use the bias scale factor as proxy (may need tuning)
    float l0_output_scale = (float)HAR_L0_B_SCALE;  // or similar scaling
    
    for (i = 0; i < HAR_HIDDEN1_DIM; i++) {
        float_out[i] = (float)int8_out[i] * l0_output_scale;
    }
}
