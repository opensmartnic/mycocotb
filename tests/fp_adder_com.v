module fp_adder_com (
    input wire clk,
    input wire rst_n,
    input [31:0] a,
    input [31:0] b,
    output reg [31:0] result,
    output reg invalid
);

    // initial begin
    //     $dumpfile("sim.vcd");
    //     $dumpvars(0, fp_adder_com);
    // end
    // 常量定义
    localparam [7:0] EXP_MAX = 8'hFF;
    localparam [3:0] ROUND_GUARD = 2;  // 保留2位保护位
    
    // 阶段1：输入分解
    wire a_sign = a[31];
    wire [7:0] a_exp = a[30:23];
    wire [23:0] a_mant = |a_exp ? {1'b1, a[22:0]} : {1'b0, a[22:0]};
    
    wire b_sign = b[31];
    wire [7:0] b_exp = b[30:23];
    wire [23:0] b_mant = |b_exp ? {1'b1, b[22:0]} : {1'b0, b[22:0]};
    
    // NaN检测
    wire a_nan = (a_exp == EXP_MAX) && |a[22:0];
    wire b_nan = (b_exp == EXP_MAX) && |b[22:0];
    
    // 阶段2：特殊值处理
    always @(*) begin
        invalid = 1'b0;
        if (a_nan || b_nan) begin
            result = {1'b0, EXP_MAX, 22'h0, 1'b1};  // 返回qNaN
            invalid = 1'b1;
        end
        else if (a_exp == EXP_MAX || b_exp == EXP_MAX) begin
            result = {a_sign ^ b_sign, EXP_MAX, 23'h0};  // 无穷处理
        end
        else begin
            // 正常数处理流程
            // [后续处理代码]
        end
    end
    
    // 阶段3：对阶处理（保留保护位）
    wire [7:0] exp_diff = a_exp - b_exp;
    wire exp_sign = exp_diff[7];
    wire [7:0] max_exp = exp_sign ? b_exp : a_exp;
    wire [4:0] shift_amount = exp_sign ? -exp_diff[4:0] : exp_diff[4:0];
    
    // 下面这种补零的方法，对于循环小数(如0.3)其实是一种近似，
    // 当做减法运算时，可能会有指数降低的情况，此时可能会产生截断误差
    wire [26:0] aligned_a_mant = exp_sign ? 
        ({a_mant, 3'b0} >> shift_amount) : {a_mant, 3'b0};
    wire [26:0] aligned_b_mant = exp_sign ? 
        {b_mant, 3'b0} : ({b_mant, 3'b0} >> shift_amount);
    
    // 阶段4：尾数运算
    
    wire operation = a_sign ^ b_sign;
    wire mant_cmp = aligned_a_mant > aligned_b_mant;
    wire [27:0] mant_adder = operation ? 
            (mant_cmp ? {1'b0, aligned_a_mant} - {1'b0, aligned_b_mant} : {1'b0, aligned_b_mant} - {1'b0, aligned_a_mant}) : 
            {1'b0, aligned_a_mant} + {1'b0, aligned_b_mant};
    wire sum_sign = operation ? (mant_cmp ? a_sign : b_sign) : a_sign;
    
    
    // 阶段5：规格化
    wire [4:0] leading_zeros;
    lzc #(.WIDTH(27)) u_lzc(mant_adder[26:0], leading_zeros);
    
    wire [26:0] normalized_mant = 
        (mant_adder[27]) ? mant_adder[27:1] :  // 溢出处理
        (mant_adder << leading_zeros);   // 前导零消除
    
    wire [7:0] normalized_exp = 
        mant_adder[27] ? max_exp + 1 : 
        (max_exp - leading_zeros);
    
    // 阶段6：舍入处理（就近舍入）
    wire round_bit = normalized_mant[ROUND_GUARD];
    wire sticky_bit = |normalized_mant[ROUND_GUARD-1:0];
    wire do_round = (round_bit && sticky_bit) || 
                   (round_bit && normalized_mant[ROUND_GUARD+1]);
    
    wire [23:0] final_mant = 
        do_round ? {1'b0, normalized_mant[25:3]} + 1 : {1'b0, normalized_mant[25:3]};
    
    // 最终结果组合
    always @(*) begin
        if (final_mant[23]) begin  // 舍入后溢出
            result[30:23] = normalized_exp + 1;
            result[22:0] = final_mant[22:0];
        end
        else begin
            result[30:23] = normalized_exp;
            result[22:0] = final_mant[22:0];
        end
        result[31] = sum_sign;
    end
    

endmodule

// 前导零计数器模块
module lzc #(
    parameter WIDTH = 28
) (
    input [WIDTH-1:0] in,
    output reg [4:0] count
);
    integer i;
    reg find;
    always @(*) begin
        count = 0;
        find = 0;
        for (i = WIDTH - 1; i >= 0; i = i - 1) begin
            if (in[i]) begin
                find = 1;
            end else begin
                if (find == 0) count = count + 1;
            end
        end
    end
endmodule