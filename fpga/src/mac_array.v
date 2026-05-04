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
    input [19455:0] weights,

    output reg [511:0] results,
    output reg mac_done
);

    localparam signed [31:0] SAT_POS = 32'sd127;
    localparam signed [31:0] SAT_NEG = -32'sd128;

    integer neuron;
    integer feature_idx;
    integer signed_sum;

    function signed [7:0] get_feature;
        input [2431:0] data;
        input [8:0] idx;
        begin
            get_feature = data[idx*8 +: 8];
        end
    endfunction

    function signed [7:0] get_weight;
        input [19455:0] data;
        input [5:0] neuron_idx;
        input [8:0] idx;
        begin
            get_weight = data[(neuron_idx*304 + idx)*8 +: 8];
        end
    endfunction

    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            results <= 512'b0;
            mac_done <= 1'b0;
        end else begin
            mac_done <= 1'b0;

            if (start) begin
                for (neuron = 0; neuron < 64; neuron = neuron + 1) begin
                    signed_sum = 0;

                    for (feature_idx = 0; feature_idx < 304; feature_idx = feature_idx + 1) begin
                        signed_sum = signed_sum +
                                     ($signed(get_feature(features, feature_idx[8:0])) *
                                      $signed(get_weight(weights, neuron[5:0], feature_idx[8:0])));
                    end

                    if (signed_sum > SAT_POS) begin
                        results[neuron*8 +: 8] <= 8'sd127;
                    end else if (signed_sum < SAT_NEG) begin
                        results[neuron*8 +: 8] <= -8'sd128;
                    end else begin
                        results[neuron*8 +: 8] <= signed_sum[7:0];
                    end
                end

                mac_done <= 1'b1;
            end
        end
    end

endmodule

