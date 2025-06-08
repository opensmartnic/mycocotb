module fp_multiplier_com (
    input wire clk,
    input wire rst_n,
    input  wire [31:0]  a,
    input  wire [31:0]  b,
    output wire [31:0]  result,
    output wire [4:0]   exceptions  // [4:IV,3:O,2:U,1:N,0:Z]
);

//---------------- 输入分解 ------------------
wire        a_sign = a[31];
wire [7:0]  a_exp  = a[30:23];
wire [22:0] a_frac = a[22:0];
wire        b_sign = b[31];
wire [7:0]  b_exp  = b[30:23];
wire [22:0] b_frac = b[22:0];

//---------------- 特殊值检测 -----------------
wire a_zero = (a_exp == 8'h0) && (a_frac == 23'h0);
wire b_zero = (b_exp == 8'h0) && (b_frac == 23'h0);
wire a_inf  = (a_exp == 8'hFF) && (a_frac == 23'h0);
wire b_inf  = (b_exp == 8'hFF) && (b_frac == 23'h0);
wire a_nan  = (a_exp == 8'hFF) && (a_frac != 23'h0);
wire b_nan  = (b_exp == 8'hFF) && (b_frac != 23'h0);

//---------------- 异常处理优先级 -----------------
wire result_nan = a_nan | b_nan | (a_inf & b_zero) | (b_inf & a_zero);
wire result_inf = (a_inf | b_inf) & ~result_nan;

//---------------- 符号计算 ----------------------
wire result_sign = a_sign ^ b_sign;

//---------------- 常规数值处理 ------------------
// 非规格化数处理
wire [7:0]  a_exp_adj = (a_exp != 0) ? a_exp : 8'h1;
wire [7:0]  b_exp_adj = (b_exp != 0) ? b_exp : 8'h1;
wire [23:0] a_man = (a_exp != 0) ? {1'b1, a_frac} : {1'b0, a_frac};
wire [23:0] b_man = (b_exp != 0) ? {1'b1, b_frac} : {1'b0, b_frac};

// 指数计算
wire [8:0] exp_sum = a_exp_adj + b_exp_adj;
wire [9:0] exp_adj = exp_sum - 10'd127;

// 尾数相乘
wire [47:0] product = a_man * b_man;

//---------------- 规格化处理 --------------------
reg [9:0]  norm_exp;
reg [26:0] norm_frac;  // 包含保护位和舍入位

always @(*) begin
    if (product[47]) begin  // 1x.xxxx格式
        norm_exp  = exp_adj + 10'd1;
        norm_frac = {product[46:24], 3'b0};  // 保留3位舍入位
    end else begin          // 0.1xxx格式
        norm_exp  = exp_adj;
        norm_frac = {product[45:23], 3'b0};  // 保留3位舍入位
    end
end

//---------------- 舍入处理 (round to nearest even) -----------
wire [23:0] rounded_frac;
wire        round_up;

assign round_up = (norm_frac[2:0] > 3'b100) || 
                 ((norm_frac[2:0] == 3'b100) && norm_frac[3]);

assign rounded_frac = round_up ? (norm_frac[26:3] + 24'h1) : norm_frac[26:3];

//---------------- 后舍入规格化 --------------------
reg [7:0]  final_exp;
reg [22:0] final_frac;

always @(*) begin
    if (rounded_frac[23]) begin  // 进位导致溢出
        final_exp  = norm_exp[7:0] + 8'h1;
        final_frac = rounded_frac[23:1];
    end else begin
        final_exp  = norm_exp[7:0];
        final_frac = rounded_frac[22:0];
    end
end

//---------------- 异常检测 ------------------------
wire invalid   = result_nan;
wire overflow  = (final_exp >= 8'hFF) & ~result_inf;
wire underflow = (norm_exp < 10'h001) & ~a_zero & ~b_zero;
wire inexact   = |product[22:0] | (|norm_frac[2:0]);

assign exceptions = {invalid, overflow, underflow, inexact, (a_zero|b_zero)};

//---------------- 最终结果选择 --------------------
assign result = result_nan ? {1'b0, 8'hFF, 23'h1} :  // 静默NaN
               result_inf ? {result_sign, 8'hFF, 23'h0} :
               (final_exp == 8'h0) ? {result_sign, 31'h0} :
               {result_sign, final_exp, final_frac};

endmodule