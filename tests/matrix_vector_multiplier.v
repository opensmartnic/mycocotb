module matrix_vector_multiplier #(
    parameter MATRIX_ROWS = 4,
    parameter MATRIX_COLS = 4
) (
    input wire clk,
    input wire rst_n,
    input wire start,
    input wire [31:0] matrix [0:MATRIX_ROWS * MATRIX_COLS-1],
    input wire [31:0] vector [0:MATRIX_COLS-1],
    output reg [31:0] result [0:MATRIX_ROWS-1],
    output reg done
);

initial begin
    $dumpfile("sim.vcd");
    $dumpvars(0, matrix_vector_multiplier);
end

// 定义浮点乘法器和加法器
wire [31:0] mult_results [0:MATRIX_ROWS-1][0:MATRIX_COLS-1];
wire [31:0] add_results [0:MATRIX_ROWS-1][0:MATRIX_COLS-1];
wire mult_done [0:MATRIX_ROWS-1][0:MATRIX_COLS-1];
wire add_done [0:MATRIX_ROWS-1][0:MATRIX_COLS-1];

// 状态机状态定义
typedef enum logic [1:0] {
    IDLE,
    MULTIPLY,
    ACCUMULATE,
    FINISH
} state_t;

state_t current_state, next_state;

// 控制信号
reg [31:0] accum [0:MATRIX_ROWS-1];
reg [$clog2(MATRIX_COLS)-1:0] col_counter;
reg [$clog2(MATRIX_ROWS)-1:0] row_counter;

genvar i, j;
// wire [MATRIX_ROWS*MATRIX_COLS-1:0] mult_done_vector;
// generate
//     for (i = 0; i < MATRIX_ROWS; i = i + 1) begin
//         for (j = 0; j < MATRIX_COLS; j = j + 1) begin
//             assign mult_done_vector[i*MATRIX_COLS + j] = mult_done[i][j];
//         end
//     end
// endgenerate
wire [MATRIX_ROWS*MATRIX_COLS-1:0] mult_done_vector = {MATRIX_ROWS*MATRIX_COLS{1'b1}};

// wire [MATRIX_ROWS*MATRIX_COLS-1:0] add_done_vector;
// generate
//     for (i = 0; i < MATRIX_ROWS; i = i + 1) begin
//         for (j = 0; j < MATRIX_COLS; j = j + 1) begin
//             assign add_done_vector[i*MATRIX_COLS + j] = add_done[i][j];
//         end
//     end
// endgenerate
wire [MATRIX_ROWS*MATRIX_COLS-1:0] add_done_vector = {MATRIX_ROWS*MATRIX_COLS{1'b1}};

// 生成乘法器
generate
    for (i = 0; i < MATRIX_ROWS; i = i + 1) begin : row_gen
        for (j = 0; j < MATRIX_COLS; j = j + 1) begin : col_gen
            fp_multiplier_com mult (
                .clk(clk),
                .rst_n(rst_n),
                .a(matrix[i * MATRIX_COLS + j]),
                .b(vector[j]),
                .result(mult_results[i][j])
                // .done(mult_done[i][j])
            );
        end
    end
endgenerate

// 生成加法器(使用流水线)
generate
    for (i = 0; i < MATRIX_ROWS; i = i + 1) begin : add_gen
        // 第一个加法器
        if (MATRIX_COLS > 1) begin
            fp_adder_com add_first (
                .clk(clk),
                .rst_n(rst_n),
                .a(mult_results[i][0]),
                .b(mult_results[i][1]),
                .result(add_results[i][1])
                // .done(add_done[i][0])
            );
        end
        
        // 中间加法器
        for (j = 2; j < MATRIX_COLS; j = j + 1) begin : mid_adders
            fp_adder_com add_mid (
                .clk(clk),
                .rst_n(rst_n),
                .a(add_results[i][j-1]),
                .b(mult_results[i][j]),
                .result(add_results[i][j])
                // .done(add_done[i][j])
            );
        end
    end
endgenerate

// 状态机
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        current_state <= IDLE;
    end else begin
        current_state <= next_state;
    end
end

// 状态转移逻辑
always @(*) begin
    case (current_state)
        IDLE: begin
            if (start) next_state = MULTIPLY; 
            else next_state = IDLE;
        end
        MULTIPLY: begin
            // 检查所有乘法是否完成
            if (&mult_done_vector) next_state = ACCUMULATE;
            else next_state = MULTIPLY;
        end
        ACCUMULATE: begin
            // 检查所有加法是否完成
            if (MATRIX_COLS == 1 || &add_done_vector) next_state = FINISH;
            else next_state = ACCUMULATE;
        end
        FINISH: next_state = IDLE;
        default: next_state = IDLE;
    endcase
end

// 输出逻辑
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        done <= 1'b0;
        for (int i = 0; i < MATRIX_ROWS; i = i + 1) begin
            result[i] <= 32'b0;
        end
    end else begin
        case (current_state)
            IDLE: begin
                done <= 1'b0;
            end
            FINISH: begin
                done <= 1'b1;
                for (int i = 0; i < MATRIX_ROWS; i = i + 1) begin
                    result[i] <= add_results[i][MATRIX_COLS-1];
                end
            end
        endcase
    end
end

endmodule