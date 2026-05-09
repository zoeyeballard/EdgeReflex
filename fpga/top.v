/*
 * top.v - Top-Level FPGA Module for Layer 1 MAC Accelerator
 * 
 * Purpose: Integrate SPI slave, weight ROM, MAC array, ReLU, and IRQ generator.
 *          Implement state machine: IDLE → RECEIVING → COMPUTING → SENDING → IDLE
 * 
 * Interface:
 *   clk       - 50 MHz DE0-CV oscillator
 *   rst_n     - Active-low reset
 *   
 *   SPI Interface (to TM4C):
 *   sclk      - SPI clock (from TM4C SSI0)
 *   cs_n      - Chip select (active-low)
 *   mosi      - Master-Out-Slave-In (304-byte feature vector)
 *   miso      - Master-In-Slave-Out (64-byte Layer 1 result)
 *   
 *   Interrupt:
 *   irq_pin   - GPIO output pulse (one per Layer 1 computation)
 * 
 * State Machine:
 *   IDLE:       Waiting for CS assertion, no computation
 *   RECEIVING:  SPI receiving 304-byte feature vector (via spi_slave)
 *   COMPUTING:  Computing Layer 1 MAC + ReLU (8 cycles after frame_done)
 *   SENDING:    SPI transmitting 64-byte result back to TM4C
 *   
 * Dataflow:
 *   1. TM4C asserts CS_n, sends 304 bytes of INT8 features over MOSI
 *   2. spi_slave receives all 304 bytes, asserts frame_done
 *   3. mac_array computes 304×64 dot products in parallel, outputs 64 INT8 values
 *   4. relu applies ReLU activation (combinational)
 *   5. irq_gen asserts irq_pin pulse for 4 cycles
 *   6. TM4C reads back 64 bytes of result over SPI (MISO)
 *   7. Return to IDLE
 * 
 * Pin Assignments (DE0-CV GPIO Header):
 *   NOTE: Exact pin numbers depend on DE0-CV board and header configuration.
 *   For Terasic DE0-CV, GPIO headers are available on GPIO0 and GPIO1 connectors.
 *   
 *   SPI Signals (example mapping to GPIO0):
 *     MOSI:  GPIO0[0]  (Pin 1 of header)
 *     MISO:  GPIO0[1]  (Pin 2 of header)
 *     SCLK:  GPIO0[2]  (Pin 3 of header)
 *     CS_N:  GPIO0[3]  (Pin 4 of header)
 *     IRQ:   GPIO0[10]  (JP1 pin 11, FPGA ball N21)
 *   
 *   These assignments must be configured in the Quartus Pin Planner.
 *   Update this file with actual pin numbers after board characterization.
 */

module top (
    input clk,                  // 50 MHz
    input rst_n,                // Active-low reset (from board reset button)
    
    // SPI Slave Interface
    input sclk,
    input cs_n,
    input mosi,
    output miso,
    
    // Interrupt Output
    output irq_pin
);

    // State machine states
    localparam IDLE       = 3'd0;
    localparam RECEIVING  = 3'd1;
    localparam COMPUTING  = 3'd2;
    localparam SENDING    = 3'd3;
    
    reg [2:0] state, next_state;
    
    // Signals between modules
    wire frame_done;
    wire send_done;
    wire mac_done;
    wire relu_done;
    
    wire [2431:0] rx_data;      // 304-byte feature vector from SPI
    wire [511:0] mac_results;   // 64-byte MAC output
    wire [511:0] relu_results;  // 64-byte ReLU output
    
    reg start_mac;
    reg tx_valid;
    
    // Instantiate SPI Slave
    spi_slave #(
        .RX_BYTES(304),
        .TX_BYTES(64)
    ) spi_slave_inst (
        .clk(clk),
        .rst_n(rst_n),
        .sclk(sclk),
        .cs_n(cs_n),
        .mosi(mosi),
        .miso(miso),
        .frame_done(frame_done),
        .send_done(send_done),
        .rx_data(rx_data),
        .tx_data(relu_results),
        .tx_valid(tx_valid)
    );
    
    // Instantiate Weight ROM (initialized from weights_hex.mif)
    // In this simplified design, weights are accessed in-line by mac_array
    // (Full integration of weight_rom would require address sequencing)
    
    // Instantiate MAC Array
    mac_array mac_array_inst (
        .clk(clk),
        .rst_n(rst_n),
        .start(start_mac),
        .features(rx_data),
        .results(mac_results),
        .mac_done(mac_done)
    );
    
    // Instantiate ReLU
    relu relu_inst (
        .clk(clk),
        .data_in(mac_results),
        .data_out(relu_results)
    );
    
    // Instantiate IRQ Generator
    irq_gen irq_gen_inst (
        .clk(clk),
        .rst_n(rst_n),
        .relu_done(relu_done),
        .irq_pin(irq_pin),
        .status_reg(),             // Not used in this baseline
        .status_read(1'b0)         // SPI read not implemented yet
    );
    
    // Delay relu_results valid by one cycle (ReLU output latency)
    reg relu_valid;
    assign relu_done = relu_valid;
    
    // State Machine
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            state <= IDLE;
            start_mac <= 1'b0;
            tx_valid <= 1'b0;
            relu_valid <= 1'b0;
        end else begin
            // Default: clear pulses. Only `start_mac` is auto-cleared here.
            // `tx_valid` and `relu_valid` are managed explicitly in states
            start_mac <= 1'b0;
            
            case (state)
                IDLE: begin
                    if (~cs_n) begin  // CS asserted
                        state <= RECEIVING;
                    end
                end
                
                RECEIVING: begin
                    if (frame_done) begin
                        // All 304 bytes received; start MAC computation
                        state <= COMPUTING;
                        start_mac <= 1'b1;
                    end
                end
                
                COMPUTING: begin
                    if (mac_done) begin
                        relu_valid <= 1'b1;  // hold high — do NOT clear in default
                        tx_valid   <= 1'b1;
                        state      <= SENDING;
                    end
                end

                SENDING: begin
                    relu_valid <= 1'b0;  // clear after irq_gen has latched it
                    tx_valid   <= 1'b0;
                    if (send_done) begin
                        state <= IDLE;
                    end
                end
                
                default: state <= IDLE;
            endcase
        end
    end

endmodule
