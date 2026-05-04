# FPGA Layer 1 MAC Accelerator — Implementation Notes

## Project Overview
EdgeReflex Part 2: FPGA-based acceleration of HAR (Human Activity Recognition) Layer 1 inference.

**Problem**: TM4C software inference at 100ms period produces:
- Inference C_i ~84ms (bound)
- Feedback C_i ~213ms at 100ms period
- Total utilization: U = 3.25 >> U_bound(n=5) = 0.743 → NOT schedulable

**Solution**: Offload Layer 1 (304×64 MAC) to DE0-CV FPGA, reducing inference C_i to <10ms.

## Architecture

### Layer Split
- **Layer 0 (FPGA)**: 304 inputs → 64 ReLU outputs (INT8 computation)
- **Layer 1 (TM4C)**: 64 inputs → 32 ReLU outputs (float on software)
- **Layer 2 (TM4C)**: 32 inputs → 6 softmax outputs (float on software)

### SPI Protocol
- **Interface**: TM4C SSI0 (SPI master) ↔ DE0-CV GPIO (SPI slave)
- **Clock**: 1 MHz (TivaWare SSI0 configured: 80 MHz / 80 = 1 MHz)
- **Mode**: SPI Mode 0 (CPOL=0, CPHA=0, MSB-first)
- **Frame Format**:
  1. TM4C asserts CS (PA3 low)
  2. TM4C sends 304 bytes (INT8 features) over MOSI
  3. FPGA processes (~50µs at 50 MHz)
  4. FPGA pulses IRQ (4 cycles on GPIO)
  5. TM4C reads 64 bytes (INT8 result) over MISO
  6. TM4C deasserts CS (PA3 high)

### Timing Assumptions

**SPI Transfer Time**:
- 304 bytes TX: 304 bytes × 8 bits/byte = 2432 bits @ 1 MHz = 2432 µs
- FPGA compute: ~50 µs (50 MHz × ~2500 cycles for MAC tree)
- 64 bytes RX: 64 bytes × 8 bits/byte = 512 bits @ 1 MHz = 512 µs
- **Total SPI + compute: ~3 ms** (well under 10 ms target)

**Firmware Cycle Count**:
- Quantization: ~304 cycles
- SPI send: 2432 µs × 80 MHz = ~194k cycles
- Wait IRQ: depends on scheduler (critical section)
- SPI recv: 512 µs × 80 MHz = ~41k cycles
- Dequantize: ~64 cycles
- Layer 2 (32→6): ~5k cycles
- **Total: ~240k cycles ~= 3 ms at 80 MHz** (target: <10 ms)

### Quantization Scheme

**Input (INT8)**:
- Scaled features (float) quantized to [-128, 127]
- Scale factor: divide by ~31.875 (assumes [-4, 4] float range)
- Formula: `int8_val = (int32_t)(float_val * 31.875) saturated`

**FPGA Computation**:
- INT8 × INT8 → INT16 products
- 304 products → INT32 accumulator per neuron
- Saturate accumulator to [-128, 127] after sum

**Output (INT8)**:
- 64 INT8 values directly from FPGA
- Dequantize by multiplying by `HAR_L0_B_SCALE` (~0.003225)
- Result fed to Layer 2 as float

## Pin Assignments (DE0-CV)

### SPI Signals (mapped to GPIO0 connector)
```
Signal   GPIO0 Pin   Function
────────────────────────────────
MOSI     Header[1]   TM4C → FPGA data
MISO     Header[2]   FPGA → TM4C data
SCLK     Header[3]   Clock from TM4C
CS_N     Header[4]   Chip select (active low)
IRQ      Header[5]   FPGA → TM4C interrupt (pulse)
GND      Header[6]   Ground reference
```

**Physical Connector**: Terasic DE0-CV GPIO0 header (40-pin dual row)
- Pin numbering: 1 (top-left), 2 (bottom-left), 3 (top-middle), etc.
- **NOTE**: Exact pin numbers depend on your GPIO0 assignment in Quartus Pin Planner.

### TM4C SSI0 Pinout (GPIO Port A)
```
Signal   Pin    Port.Pin   Function
─────────────────────────────────────
SCLK     PA2    GPIO_PIN_2   Output
MOSI     PA5    GPIO_PIN_5   Output
MISO     PA4    GPIO_PIN_4   Input
CS       PA3    GPIO_PIN_3   Output (GPIO, not SSI)
```

### TM4C IRQ Input (GPIO Port B)
```
Signal   Pin    Port.Pin   Function
─────────────────────────────────────
IRQ      PB4    GPIO_PIN_4   Input (rising edge interrupt)
```

## Verilog Modules

### spi_slave.v
- **Clock**: 50 MHz (DE0-CV)
- **States**: IDLE → RX_FRAME → TX_FRAME → DONE
- **RX Phase**: Sample MOSI on SCLK rising edge, accumulate into 304-byte array
- **TX Phase**: Drive MISO with Layer 1 result bytes on SCLK rising edge
- **Metastability Protection**: 2-FF synchronizers on all SPI inputs
- **Pulses**: `frame_done` asserted 1 cycle when RX complete; `send_done` when TX complete

### mac_array.v
- **Architecture**: Folded parallel MAC (pipelined sequentially over ~38 cycles)
- **Input**: 304×8-bit features, 64×304×8-bit weights (all parallel)
- **Output**: 64×8-bit INT8 results, saturated from INT32 accumulators
- **Pipeline Depth**: 8 cycles (fixed latency for timing predictability)
- **Saturation**: If acc > 127 → 127; if acc < -128 → -128; else acc[7:0]

### relu.v
- **Type**: Combinational (with optional output register for timing)
- **Operation**: output[i] = (input[i] > 0) ? input[i] : 0 for 64 elements
- **Timing**: <1 ns combinational + 1 clk if registered

### irq_gen.v
- **Pulse Width**: 4 cycles at 50 MHz = 80 ns
- **Status Register**: 
  - Bit 0: IRQ pending (set on relu_done, cleared on status read)
  - Bit 1: IRQ pulsed (set during pulse_count > 0)
- **Integration**: Can be read by TM4C over SPI (future enhancement)

### top.v
- **State Machine**: IDLE → RECEIVING → COMPUTING → SENDING → IDLE
- **Dataflow**:
  1. Wait for CS assertion (IDLE → RECEIVING)
  2. Receive 304 bytes into rx_data (RECEIVING)
  3. On frame_done, trigger mac_array (RECEIVING → COMPUTING)
  4. On mac_done, load relu, pulse irq (COMPUTING → SENDING)
  5. On send_done or CS deassertion, return IDLE
- **Interconnect**: All 5 modules connected; fixed 50 MHz clock; synchronous design

## Testing & Simulation

### ModelSim Testbenches
1. **tb_spi_slave.v**: Test 304-byte RX and 64-byte TX transactions
2. **tb_relu.v**: Test elementwise ReLU on positive/negative values
3. **tb_mac_array.v**: Verify dot product computation (simple example)
4. **tb_irq_gen.v**: Verify 4-cycle pulse and status register
5. **tb_top.v**: End-to-end SPI transaction with IRQ

### Simulation Workflow (before hardware)
```bash
# Compile all modules and testbenches
vlib work
vmap work ./work
vlog spi_slave.v weight_rom.v mac_array.v relu.v irq_gen.v top.v
vlog tb_*.v

# Run each testbench
vsim -gui tb_spi_slave
vsim -gui tb_relu
vsim -gui tb_mac_array
vsim -gui tb_irq_gen
vsim -gui tb_top

# Check results in Wave window
```

## Hardware Integration Checklist

- [ ] Verify GPIO0 pin assignments in Quartus match this document
- [ ] Compile Quartus project to FPGA bitstream
- [ ] Program DE0-CV FPGA via USB-Blaster
- [ ] Connect TM4C SSI0 pins (PA2, PA3, PA4, PA5) to GPIO0 SPI pins
- [ ] Connect TM4C GPIO PB4 to GPIO0 IRQ pin (pull-up recommended)
- [ ] Compile TM4C firmware with INFERENCE_USE_SPI_FPGA=1
- [ ] Program TM4C via JTAG
- [ ] Verify communication on logic analyzer (HiLetgo)
  - SCLK frequency: 1 MHz square wave
  - MOSI: 304 bytes of rising/falling transitions
  - MISO: 64 bytes response after IRQ pulse
  - IRQ: 4-cycle pulse (~80 ns)
- [ ] Measure end-to-end cycle count in DWT logs
- [ ] Run schedulability analysis with new WCET numbers

## Known Limitations & Future Work

1. **Quantization Mismatch**: 
   - Current dequantize uses `HAR_L0_B_SCALE` as proxy; may need tuning for accuracy
   - Consider generating quantization tables from training pipeline
   - Measure accuracy loss before full deployment

2. **Weight Regeneration**:
   - Current weights_hex.mif are for 561×64 Layer 0
   - If reducing to 304 inputs, retrain and regenerate weights for better accuracy
   - Current approach uses only first 304 rows of existing weights

3. **SPI Clock Rate**:
   - Currently 1 MHz; could be increased to 2-4 MHz if timing permits
   - DE0-CV and TM4C both support higher rates
   - Trade-off: faster = lower latency, but more susceptible to noise

4. **Parallel MAC Implementation**:
   - Current mac_array.v is simplified; real design needs Kogge-Stone adder tree
   - 304-input parallel prefix adder would fit in Cyclone V LEs (~1000)
   - Reduces pipeline depth to 2-3 cycles instead of 8

5. **Cache Optimization**:
   - Preload weights into FPGA BRAM at startup
   - Cache Layer 2 weights in TM4C TCM if available

## References

- TM4C123GH6PM LaunchPad User Guide: SSI0 on GPIO Port A
- Terasic DE0-CV Manual: GPIO header pinouts, FPGA resource counts
- HAR Model: 561→64→32→6 MLP, INT8 quantization, weights in weights_hex.mif
- FreeRTOS Timing: Critical sections, semaphore handoff latency
