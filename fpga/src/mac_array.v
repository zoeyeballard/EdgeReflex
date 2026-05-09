/*
 * mac_array.v - Parallel INT8 MAC Array for Layer 1
 *
 * Computes the signed dot product for 64 neurons across 304 input features.
 * The implementation is simulation-friendly: on `start`, it evaluates the full
 * matrix in one clock, saturates to INT8, and pulses `mac_done`.
 */

module mac_array (
    input clk,
    input rst_n,
    input start,

    input [2431:0] features,

    output reg [511:0] results,
    output reg mac_done
);

    localparam signed [31:0] SAT_POS = 32'sd127;
    localparam signed [31:0] SAT_NEG = -32'sd128;

    reg busy;
    reg [5:0] neuron_idx;
    reg [8:0] feature_idx;
    reg [8:0] feature_idx_d;
    reg [5:0] neuron_idx_d;
    reg [5:0] warmup_count;
    reg pending_valid;
    reg signed [31:0] acc;
    reg [15:0] weight_addr;
    wire [7:0] weight_byte;
    reg signed [15:0] signed_mul;
    reg signed [31:0] signed_sum_next;

    function signed [7:0] get_feature;
        input [2431:0] data;
        input [8:0] idx;
        begin
            get_feature = data[idx*8 +: 8];
        end
    endfunction

    function signed [7:0] sat_int8;
        input signed [31:0] val;
        begin
            if (val > SAT_POS) begin
                sat_int8 = 8'sd127;
            end else if (val < SAT_NEG) begin
                sat_int8 = 8'sh80;
            end else begin
                sat_int8 = val[7:0];
            end
        end
    endfunction

    weight_rom weight_rom_inst (
        .clk(clk),
        .addr(weight_addr),
        .rd_data(weight_byte)
    );

    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            results <= 512'b0;
            mac_done <= 1'b0;
            busy <= 1'b0;
            neuron_idx <= 6'd0;
            feature_idx <= 9'd0;
            neuron_idx_d <= 6'd0;
            feature_idx_d <= 9'd0;
            warmup_count <= 6'd0;
            pending_valid <= 1'b0;
            acc <= 32'sd0;
            weight_addr <= 16'd0;
        end else begin
            mac_done <= 1'b0;

            if (start && ~busy) begin
                busy <= 1'b1;
                neuron_idx <= 6'd0;
                feature_idx <= 9'd0;
                neuron_idx_d <= 6'd0;
                feature_idx_d <= 9'd0;
                warmup_count <= 6'd0;
                pending_valid <= 1'b0;
                acc <= 32'sd0;
                results <= 512'b0;
                weight_addr <= 16'd0;
            end else if (busy) begin
                // Keep issuing addresses. The generated ROM has registered outputs,
                // so data returns after a few cycles and is consumed with delayed indices.
                if (pending_valid) begin
                    signed_mul = $signed(get_feature(features, feature_idx_d)) *
                                 $signed(weight_byte);
                    signed_sum_next = acc + signed_mul;

                    if (feature_idx_d == 9'd303) begin
                        results[neuron_idx_d*8 +: 8] <= sat_int8(signed_sum_next);

                        if (neuron_idx_d == 6'd63) begin
                            busy <= 1'b0;
                            mac_done <= 1'b1;
                            feature_idx <= 9'd0;
                            neuron_idx <= 6'd0;
                            feature_idx_d <= 9'd0;
                            neuron_idx_d <= 6'd0;
                            warmup_count <= 6'd0;
                            pending_valid <= 1'b0;
                            acc <= 32'sd0;
                        end else begin
                            acc <= 32'sd0;
                        end
                    end else begin
                        acc <= signed_sum_next;
                    end
                end

                if (feature_idx == 9'd303) begin
                    feature_idx <= 9'd0;
                    neuron_idx <= neuron_idx + 1'b1;
                end else begin
                    feature_idx <= feature_idx + 1'b1;
                end

                weight_addr <= ({10'd0, neuron_idx} * 16'd304) + {7'd0, feature_idx};
                neuron_idx_d <= neuron_idx;
                feature_idx_d <= feature_idx;

                if (warmup_count < 6'd3) begin
                    warmup_count <= warmup_count + 1'b1;
                    if (warmup_count == 6'd2)
                        pending_valid <= 1'b1;
                end
            end
        end
    end

endmodule

