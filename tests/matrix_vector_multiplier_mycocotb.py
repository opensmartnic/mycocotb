import mycocotb as cocotb
from mycocotb.triggers import RisingEdge, FallingEdge, Timer
import numpy as np
import sys
import debugpy
debugpy.listen(4000)
print('Waiting for client...')
# debugpy.wait_for_client()

# 定义矩阵和向量的维度
MATRIX_ROWS = 4
MATRIX_COLS = 4

# 定义FP32的位宽
FP32_WIDTH = 32

# 将浮点数转换为32位二进制表示
def float_to_bin32(value):
    return int.from_bytes(np.float32(value).tobytes(), byteorder='little')

# 将32位二进制表示转换为浮点数
def bin32_to_float(value):
    return np.frombuffer(value.to_bytes(byteorder='little'), dtype=np.float32)[0]

# 生成随机矩阵和向量
def generate_random_matrix(rows, cols):
    return np.random.randn(rows, cols).astype(np.float32)
    # return 

def generate_random_vector(size):
    return np.random.randn(size).astype(np.float32)
    # return 
# 计算参考结果
def compute_reference_result(matrix, vector):
    return np.dot(matrix, vector)

async def clock_gen(dut):
    while True:
        dut.clk.value = 0
        await Timer(5)
        dut.clk.value = 1
        await Timer(5)

async def test_matrix_vector_multiplier(dut):
    # 初始化时钟
    cocotb.start_soon(clock_gen(dut))

    # 复位模块
    dut.rst_n.value = 0
    await RisingEdge(dut.clk)
    await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    # 测试数据
    test_data = [
        (np.eye(MATRIX_ROWS, MATRIX_COLS).astype(np.float32), np.ones((MATRIX_ROWS, )).astype(np.float32)),
        (generate_random_matrix(MATRIX_ROWS, MATRIX_COLS), generate_random_vector(MATRIX_COLS))
    ]
    for matrix, vector in test_data:
        # 将矩阵和向量加载到DUT
        for i in range(MATRIX_ROWS):
            for j in range(MATRIX_COLS):
                dut.matrix[i*MATRIX_COLS + j].value = float_to_bin32(matrix[i][j])
        for j in range(MATRIX_COLS):
            dut.vector[j].value = float_to_bin32(vector[j])

        # 启动计算
        dut.start.value = 1
        await RisingEdge(dut.clk)
        dut.start.value = 0

        # 等待计算完成
        while dut.done.value != 1:
            await RisingEdge(dut.clk)

        # 读取结果并转换为浮点数
        result = np.zeros(MATRIX_ROWS, dtype=np.float32)
        for i in range(MATRIX_ROWS):
            print(f'dut.result[i].value = {dut.result[i].value}')
            # rpdb.set_trace()
            result[i] = bin32_to_float(dut.result[i].value)

        # 计算参考结果
        reference_result = compute_reference_result(matrix, vector)

        # 验证结果
        assert np.allclose(result, reference_result, rtol=1e-5), \
            f"Test failed: Expected {reference_result}, got {result}"

        # 打印测试结果
        print("Test passed!")
        print("Matrix:")
        print(matrix)
        print("Vector:")
        print(vector)
        print("Result:")
        print(result)
        print("Reference Result:")
        print(reference_result)
    # 因为我们没有使用cocotb的test装饰器，所以需要手动退出，
    # 否则会因为时钟一直在运行而导致测试无法结束
    sys.exit()

cocotb.start_soon(test_matrix_vector_multiplier(cocotb.top))