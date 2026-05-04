/*
 * relu.v - Rectified Linear Unit (ReLU) Activation
 * 
 * Purpose: Apply ReLU activation to 64 INT8 values
 *          output[i] = (input[i] > 0) ? input[i] : 0
 * 
 * Interface:
 *   clk       - 50 MHz clock
 *   data_in   - 64×8 = 512-bit input (64 signed INT8 values)
 *   data_out  - 64×8 = 512-bit output (ReLU applied elementwise)
 * 
 * Timing: Combinational (single cycle)
 *         Or pipelined by one cycle if needed (add register stage)
 */

module relu #(
    parameter WIDTH = 512,      // 64 × 8 bits
    parameter NEURONS = 64
)(
    input clk,
    input [WIDTH-1:0] data_in,
    output reg [WIDTH-1:0] data_out
);

    integer i;
    genvar j;
    wire signed [7:0] in_val [NEURONS-1:0];
    wire signed [7:0] out_val [NEURONS-1:0];
    
    // Unpack input
    generate
        for (j = 0; j < NEURONS; j = j + 1) begin : unpack_in
            assign in_val[j] = data_in[j*8 +: 8];
        end
    endgenerate
    
    // Apply ReLU combinationally (or with optional pipeline register)
    generate
        for (j = 0; j < NEURONS; j = j + 1) begin : relu_logic
            assign out_val[j] = (in_val[j] > 8'sd0) ? in_val[j] : 8'sd0;
        end
    endgenerate
    
    // Register output (optional: add one-cycle latency for timing)
    always @(posedge clk) begin
        for (i = 0; i < NEURONS; i = i + 1) begin
            data_out[i*8 +: 8] <= out_val[i];
        end
    end

endmodule

