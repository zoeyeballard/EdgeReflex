/*
 * spi_slave.v - SPI Slave Module for Feature Vector Reception
 *
 * Receives a 304-byte feature vector, but also tolerates shorter simulation
 * transactions by closing the frame on CS deassertion or a long idle gap.
 * Once Layer 1 results are latched, the slave shifts them out in SPI mode 0.
 */

module spi_slave #(
    parameter RX_BYTES = 304,
    parameter TX_BYTES = 64,
    parameter FRAME_GAP_CYCLES = 6'd32,
    parameter DONE_HOLD_CYCLES = 6'd16
)(
    input clk,
    input rst_n,

    input sclk,
    input cs_n,
    input mosi,
    output reg miso,

    output reg frame_done,
    output reg send_done,

    output reg [RX_BYTES*8-1:0] rx_data,
    input [TX_BYTES*8-1:0] tx_data,
    input tx_valid
);

    reg sclk_r, sclk_r2;
    reg cs_r, cs_r2;
    reg mosi_r, mosi_r2;

    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            sclk_r <= 1'b0;
            sclk_r2 <= 1'b0;
            cs_r <= 1'b1;
            cs_r2 <= 1'b1;
            mosi_r <= 1'b0;
            mosi_r2 <= 1'b0;
        end else begin
            sclk_r <= sclk;
            sclk_r2 <= sclk_r;
            cs_r <= cs_n;
            cs_r2 <= cs_r;
            mosi_r <= mosi;
            mosi_r2 <= mosi_r;
        end
    end

    wire sclk_rise = (~sclk_r2) & sclk_r;
    wire sclk_fall = sclk_r2 & (~sclk_r);
    wire cs_fall = cs_r2 & (~cs_r);
    wire cs_rise = (~cs_r2) & cs_r;

    reg [7:0] rx_shift;
    reg [7:0] tx_shift;
    reg [11:0] rx_byte_count;
    reg [5:0] tx_byte_count;
    reg [2:0] rx_bit_count;
    reg [2:0] tx_bit_count;
    reg [5:0] idle_count;
    reg [5:0] frame_done_cnt;
    reg [5:0] send_done_cnt;
    reg rx_started;
    reg rx_complete;
    reg tx_active;
    reg tx_ready;
    reg [TX_BYTES*8-1:0] tx_buffer;

    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            frame_done <= 1'b0;
            send_done <= 1'b0;
            miso <= 1'b0;
            rx_data <= {RX_BYTES*8{1'b0}};
            rx_shift <= 8'b0;
            tx_shift <= 8'b0;
            rx_byte_count <= 12'b0;
            tx_byte_count <= 6'b0;
            rx_bit_count <= 3'b0;
            tx_bit_count <= 3'b0;
            idle_count <= 6'b0;
            frame_done_cnt <= 6'b0;
            send_done_cnt <= 6'b0;
            rx_started <= 1'b0;
            rx_complete <= 1'b0;
            tx_active <= 1'b0;
            tx_ready <= 1'b0;
            tx_buffer <= {TX_BYTES*8{1'b0}};
        end else begin
            // update done counters and outputs
            if (frame_done_cnt != 3'b0)
                frame_done_cnt <= frame_done_cnt - 1'b1;
            if (send_done_cnt != 3'b0)
                send_done_cnt <= send_done_cnt - 1'b1;
            frame_done <= (frame_done_cnt != 3'b0);
            send_done <= (send_done_cnt != 3'b0);

            if (tx_valid) begin
                tx_buffer <= tx_data;
                tx_ready <= 1'b1;
            end

            if (cs_fall) begin
                rx_shift <= 8'b0;
                tx_shift <= 8'b0;
                rx_byte_count <= 12'b0;
                tx_byte_count <= 6'b0;
                rx_bit_count <= 3'b0;
                tx_bit_count <= 3'b0;
                idle_count <= 6'b0;
                frame_done_cnt <= 6'b0;
                send_done_cnt <= 6'b0;
                rx_started <= 1'b0;
                rx_complete <= 1'b0;
                tx_active <= 1'b0;
                miso <= 1'b0;
            end

            if (~cs_r) begin
                if (sclk_rise || sclk_fall) begin
                    idle_count <= 6'b0;
                end else if (idle_count != FRAME_GAP_CYCLES) begin
                    idle_count <= idle_count + 1'b1;
                end

                if (idle_count == FRAME_GAP_CYCLES - 1'b1 && rx_started && ~rx_complete) begin
                    frame_done_cnt <= DONE_HOLD_CYCLES;
                    frame_done <= 1'b1;
                    rx_complete <= 1'b1;
                end

                if (~tx_active && sclk_rise) begin
                    rx_started <= 1'b1;
                    rx_shift <= {rx_shift[6:0], mosi_r2};

                    if (rx_bit_count == 3'd7) begin
                        rx_data <= { rx_data[(RX_BYTES*8-9) : 0], {rx_shift[6:0], mosi_r2} };
                        rx_byte_count <= rx_byte_count + 1'b1;
                        rx_bit_count <= 3'b0;

                        if (rx_byte_count + 1'b1 == RX_BYTES) begin
                            frame_done_cnt <= DONE_HOLD_CYCLES;
                            frame_done <= 1'b1;
                            rx_complete <= 1'b1;
                        end
                    end else begin
                        rx_bit_count <= rx_bit_count + 1'b1;
                    end
                end

                if (~tx_active && rx_complete && tx_ready) begin
                    tx_active <= 1'b1;
                    tx_ready <= 1'b0;
                    tx_byte_count <= 6'b0;
                    tx_bit_count <= 3'b0;
                    tx_shift <= tx_buffer[7:0];
                    miso <= tx_buffer[7];
                end

                if (tx_active && sclk_fall) begin
                    if (tx_bit_count == 3'd7) begin
                            if (tx_byte_count + 1'b1 == TX_BYTES) begin
                            tx_active <= 1'b0;
                            tx_byte_count <= 6'b0;
                            tx_bit_count <= 3'b0;
                            send_done_cnt <= DONE_HOLD_CYCLES;
                            send_done <= 1'b1;
                        end else begin
                            tx_byte_count <= tx_byte_count + 1'b1;
                            tx_bit_count <= 3'b0;
                            tx_shift <= tx_buffer[15:8];
                            miso <= tx_buffer[15];
                            tx_buffer <= tx_buffer >> 8;
                        end
                    end else begin
                        tx_bit_count <= tx_bit_count + 1'b1;
                        tx_shift <= {tx_shift[6:0], 1'b0};
                        miso <= tx_shift[6];
                    end
                end
            end

            if (cs_rise) begin
                if (rx_started) begin
                    frame_done_cnt <= DONE_HOLD_CYCLES;
                    frame_done <= 1'b1;
                end
                if (tx_active || tx_byte_count != 6'b0 || tx_bit_count != 3'b0) begin
                    send_done_cnt <= DONE_HOLD_CYCLES;
                    send_done <= 1'b1;
                end

                rx_started <= 1'b0;
                rx_complete <= 1'b0;
                tx_active <= 1'b0;
                tx_ready <= 1'b0;
                rx_byte_count <= 12'b0;
                tx_byte_count <= 6'b0;
                rx_bit_count <= 3'b0;
                tx_bit_count <= 3'b0;
                idle_count <= 6'b0;
                miso <= 1'b0;
            end
        end
    end

endmodule
