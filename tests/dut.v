`timescale 1ns/1ns
module dut;
    reg a;
    reg b;
    always @(a) begin
        b = a;
        $display("b = %d, a = %d", b, a);
    end
    initial begin
        #0 a = 1'b0;
        #5 a = ~a;
        #10 a = ~a;
    end    
endmodule