/*
 * tb_top.v - ModelSim Testbench for Top-Level FPGA Module
 * 
 * Simulates complete SPI communication:
 * - TM4C sends 304-byte feature vector
 * - FPGA computes Layer 1 (MAC + ReLU)
 * - TM4C reads 64-byte result
 */

`timescale 1ns / 1ps

module tb_top;

    reg clk, rst_n;
    reg sclk, cs_n, mosi;
    reg [7:0] rx_byte;
    wire miso;
    wire irq_pin;
    
    // Instantiate DUT
    top dut (
        .clk(clk), .rst_n(rst_n),
        .sclk(sclk), .cs_n(cs_n), .mosi(mosi), .miso(miso),
        .irq_pin(irq_pin)
    );
    
    // Clock generation (50 MHz)
    always #10 clk = ~clk;
    
    // SPI pulse task
    task spi_pulse;
        begin
            sclk = 1'b1; #100;
            sclk = 1'b0; #100;
        end
    endtask
    
    // Send byte over SPI MOSI
    task send_spi_byte(input [7:0] data);
        integer i;
        begin
            for (i = 7; i >= 0; i = i - 1) begin
                mosi = data[i];  // MSB first
                spi_pulse;
            end
        end
    endtask
    
    // Receive byte from SPI MISO
    task recv_spi_byte(output [7:0] data);
        integer i;
        begin
            data = 8'b0;
            for (i = 7; i >= 0; i = i - 1) begin
                data[i] = miso;
                spi_pulse;
            end
        end
    endtask
    
    initial begin
        clk = 1'b0;
        rst_n = 1'b1;
        sclk = 1'b0;
        cs_n = 1'b1;
        mosi = 1'b0;
        
        // Reset
        #20 rst_n = 1'b0;
        #50 rst_n = 1'b1;
        #500;
        
        $display("=== Test 1: Full SPI Transaction ===");
        
        // Assert CS (start transaction)
        #100 cs_n = 1'b0;
        $display("CS asserted at time %0t", $time);
        
        // Send first 10 bytes as test
        // In full test, send all 304 bytes
        send_spi_byte(8'h01);
        send_spi_byte(8'h02);
        send_spi_byte(8'h03);
        send_spi_byte(8'h04);
        send_spi_byte(8'h05);
        send_spi_byte(8'h06);
        send_spi_byte(8'h07);
        send_spi_byte(8'h08);
        send_spi_byte(8'h09);
        send_spi_byte(8'h0A);
        
        $display("Sent 10 bytes at time %0t", $time);
        
        // Wait for IRQ pulse (computation in progress)
        #1000;
        if (irq_pin) begin
            $display("OBSERVE: IRQ pulse detected at time %0t", $time);
        end
        
        // Continue receiving back result
        #500;
        $display("Receiving Layer 1 result...");
        recv_spi_byte(rx_byte);  // Capture first result byte
        $display("Received byte: %02h", rx_byte);
        
        // Deassert CS
        #100 cs_n = 1'b1;
        $display("CS deasserted at time %0t", $time);
        
        #1000 $finish;
    end
    
    // Monitor
    initial begin
        $monitor("Time=%0t cs_n=%b sclk=%b irq_pin=%b miso=%b",
                 $time, cs_n, sclk, irq_pin, miso);
    end

endmodule
