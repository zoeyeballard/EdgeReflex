/*
 * irq_gen.v - IRQ Generator with Status Register
 * 
 * Purpose: Generate a 4-cycle GPIO pulse on the IRQ output line when relu_done asserted.
 *          Also provides a status register that TM4C can read over SPI to confirm IRQ.
 * 
 * Interface:
 *   clk          - 50 MHz clock
 *   rst_n        - Active-low reset
 *   relu_done    - Input pulse: computation complete
 *   irq_pin      - Output: GPIO line to TM4C interrupt input (pulses for 4 cycles)
 *   status_reg   - Output: 8-bit status register readable by TM4C
 *                  Bit 0: IRQ pending (set when relu_done seen, cleared by status read)
 *                  Bit 1: IRQ was pulsed (set during pulse_count > 0)
 *                  Bits 7-2: Reserved (0)
 * 
 * Timing:
 *   When relu_done asserted:
 *   - Set IRQ pending bit
 *   - Start 4-cycle pulse on irq_pin
 *   - Clear pending bit on next SPI read of status_reg
 */

module irq_gen (
    input clk,
    input rst_n,
    input relu_done,           // Pulse from ReLU when done
    output reg irq_pin,        // GPIO to TM4C
    output reg [7:0] status_reg,
    input status_read          // SPI read strobe
);

    // Pulse counter (counts down from 4 to 0)
    reg [2:0] pulse_count;
    
    // Status bits
    reg irq_pending;
    reg irq_pulsed;
    
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            pulse_count <= 3'b0;
            irq_pending <= 1'b0;
            irq_pulsed <= 1'b0;
            irq_pin <= 1'b0;
            status_reg <= 8'b0;
        end else begin
            // Handle relu_done pulse
            if (relu_done) begin
                irq_pending <= 1'b1;
                pulse_count <= 3'd4;  // Start 4-cycle pulse
            end
            
            // Pulse generation
            if (pulse_count > 3'b0) begin
                irq_pin <= 1'b1;
                pulse_count <= pulse_count - 1'b1;
                irq_pulsed <= 1'b1;
            end else begin
                irq_pin <= 1'b0;
                irq_pulsed <= 1'b0;
            end
            
            // Status register updates
            if (status_read) begin
                // Clear pending bit on read
                irq_pending <= 1'b0;
            end
            
            // Pack status register
            status_reg <= {6'b0, irq_pulsed, irq_pending};
        end
    end

endmodule
