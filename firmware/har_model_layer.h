/*
 * har_model_layer.h - Split Layer 1/Layer 2 HAR Model Interface
 * 
 * Purpose: Allow Layer 1 computation to be offloaded to FPGA,
 *          while Layer 2 runs on TM4C.
 * 
 * Architecture:
 *   Layer 0 (offloaded to FPGA):  561 -> 64 (ReLU)
 *   Layer 1 (TM4C software):      64 -> 32 (ReLU)
 *   Layer 2 (TM4C software):      32 -> 6  (softmax argmax)
 */

#ifndef HAR_MODEL_LAYER_H
#define HAR_MODEL_LAYER_H

#include <stdint.h>

/*
 * Initialize Layer 1/Layer 2 split model.
 * Must call this once before any layer functions.
 */
void HarModelLayerInit(void);

/*
 * Preprocessing: Standardize raw input features.
 * 
 * Input:  raw[561] - raw sensor features from IMU pipeline
 * Output: scaled[561] - standardized features
 * 
 * This is the same standardization as in the original har_infer.
 */
void HarPreprocess(const float *raw, float *scaled);

/*
 * Layer 2 inference: 64-dim float input -> 6-dim class probabilities
 * 
 * Input:  h1[64] - Layer 1 output (32-bit float or dequantized from INT8)
 * Output: out[6] - class scores (will run argmax internally)
 * 
 * Returns: predicted class index (0..5)
 */
int HarInferenceLayer2(const float *h1);

/*
 * Convert Layer 1 output from INT8 (FPGA) to float for Layer 2.
 * 
 * The FPGA outputs INT8 values with implicit scale factor HAR_L0_OUT_SCALE.
 * This function converts them back to float for Layer 2.
 * 
 * Input:  int8_out[64] - Layer 1 result from FPGA (INT8)
 * Output: float_out[64] - converted to float with proper scaling
 */
void HarDequantizeLayer1(const int8_t *int8_out, float *float_out);

#endif  // HAR_MODEL_LAYER_H
