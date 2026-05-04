/*
 * tb_mac_array.v - ModelSim Testbench for MAC Array Module
 * 
 * Tests parallel MAC computation for Layer 1
 */

`timescale 1ns / 1ps

module tb_mac_array;

    reg clk, rst_n;
    reg start;
    reg [2431:0] features;    // 304 × 8-bit
    reg [19455:0] weights;    // 64 × 304 × 8-bit
    wire [511:0] results;     // 64 × 8-bit
    wire mac_done;
    
    // Instantiate DUT
    mac_array dut (
        .clk(clk), .rst_n(rst_n),
        .start(start),
        .features(features),
        .weights(weights),
        .results(results),
        .mac_done(mac_done)
    );
    
    // Clock generation (50 MHz)
    always #10 clk = ~clk;
    
    // Helper to set feature value
    task set_feature(input [8:0] idx, input signed [7:0] val);
        features[idx*8 +: 8] = val;
    endtask
    
    // Helper to set weight value
    task set_weight(input [5:0] neuron, input [8:0] feat_idx, input signed [7:0] val);
        weights[(neuron*304 + feat_idx)*8 +: 8] = val;
    endtask
    
    initial begin
        clk = 1'b0;
        rst_n = 1'b1;
        start = 1'b0;
        features = 2432'b0;
        weights = 19456'b0;
        
        // Reset
        #20 rst_n = 1'b0;
        #50 rst_n = 1'b1;
        #100;
        
        $display("=== Test 1: Simple dot product ===");
        $display("Feature vector: [1, 2, 3, 0, 0, ...]");
        $display("Neuron 0 weights: [2, 2, 2, 0, 0, ...]");
        $display("Expected result: 1*2 + 2*2 + 3*2 = 12");
        
        // Initialize features: [1, 2, 3, 0, ...]
        set_feature(0, 8'sd1);
        set_feature(1, 8'sd2);
        set_feature(2, 8'sd3);
        
        // Initialize weights for neuron 0: [2, 2, 2, 0, ...]
        set_weight(0, 0, 8'sd2);
        set_weight(0, 1, 8'sd2);
        set_weight(0, 2, 8'sd2);
        
        // Start computation
        #100 start = 1'b1;
        #20  start = 1'b0;
        
        // Wait for mac_done (should take ~40 cycles based on pipeline)
        wait (mac_done == 1'b1);
        #20;
        
        $display("MAC done after pipeline delay");
        $display("Result[0] (neuron 0): %d (signed)", $signed(results[7:0]));
        
        if ($signed(results[7:0]) == 8'sd12) begin
            $display("PASS: Dot product computed correctly");
        end else begin
            $display("FAIL: Expected 12, got %d", $signed(results[7:0]));
        end
        
        #500 $finish;
    end
    
    // Monitor
    initial begin
        $monitor("Time=%0t start=%b mac_done=%b results[0]=%d",
                 $time, start, mac_done, $signed(results[7:0]));
    end

endmodule
