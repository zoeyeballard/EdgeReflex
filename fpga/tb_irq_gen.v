/*
 * tb_irq_gen.v - ModelSim Testbench for IRQ Generator Module
 */

`timescale 1ns / 1ps

module tb_irq_gen;

    reg clk, rst_n;
    reg relu_done;
    wire irq_pin;
    wire [7:0] status_reg;
    reg status_read;
    
    // Instantiate DUT
    irq_gen dut (
        .clk(clk), .rst_n(rst_n),
        .relu_done(relu_done),
        .irq_pin(irq_pin),
        .status_reg(status_reg),
        .status_read(status_read)
    );
    
    // Clock generation (50 MHz)
    always #10 clk = ~clk;
    
    initial begin
        clk = 1'b0;
        rst_n = 1'b1;
        relu_done = 1'b0;
        status_read = 1'b0;
        
        // Reset
        #20 rst_n = 1'b0;
        #50 rst_n = 1'b1;
        #100;
        
        $display("=== Test 1: IRQ Pulse Generation ===");
        
        // Assert relu_done
        #100 relu_done = 1'b1;
        #20  relu_done = 1'b0;
        
        $display("Time %0t: relu_done pulsed", $time);
        
        // Wait and observe IRQ pulse (should be 4 cycles)
        repeat (10) begin
            #20 $display("Time %0t: irq_pin=%b status_reg=%08b",
                         $time, irq_pin, status_reg);
        end
        
        $display("=== Test 2: Status Register Read ===");
        
        #100 relu_done = 1'b1;
        #20  relu_done = 1'b0;
        
        #100;
        $display("Before status_read: status_reg=%08b", status_reg);
        
        #20 status_read = 1'b1;
        #20 status_read = 1'b0;
        
        #20;
        $display("After status_read: status_reg=%08b", status_reg);
        
        #500 $finish;
    end

endmodule
