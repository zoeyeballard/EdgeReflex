/*
 * weight_rom.v - Simulation wrapper for the synthesized weights ROM IP
 *
 * This keeps the legacy module name `weight_rom` while routing reads through
 * the Quartus-generated `weights_rom` block that is initialized from
 * `weights_hex.mif`.
 */

module weight_rom (
    input clk,
    input [15:0] addr,
    output [7:0] rd_data
);

    weights_rom rom_inst (
        .address(addr),
        .clock(clk),
        .q(rd_data)
    );

endmodule
