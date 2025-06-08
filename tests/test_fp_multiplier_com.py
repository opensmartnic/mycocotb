import cocotb
from cocotb.triggers import RisingEdge, Timer
from cocotb.clock import Clock
import numpy as np

# 将浮点数转换为32位二进制表示
def float_to_bin32(value):
    return int.from_bytes(np.float32(value).tobytes(), byteorder='little')

# 将32位二进制表示转换为浮点数
def bin32_to_float(value):
    return np.frombuffer(value.to_bytes(length=4, byteorder='little'), dtype=np.float32)[0]

@cocotb.test()
async def test_fp_multiplier_com(dut):
    # 启动时钟
    clock = Clock(dut.clk, 10, units="ns")  # 10 ns 时钟周期
    cocotb.start_soon(clock.start())

    # 复位
    dut.rst_n.value = 0
    await RisingEdge(dut.clk)
    dut.rst_n.value = 1

    # 等待一段时间以稳定状态
    await RisingEdge(dut.clk)

    # 测试数据
    test_data = [
        (1.0, 1.0),  
        (0.5, 0.25),
        (0.3, -0.25),
        (1.0, -0.01),
        (np.random.rand(), np.random.rand() - 1),
        (np.random.rand() - 0.5, np.random.rand() - 0.5)
    ]

    for a, b in test_data:
        # 设置输入
        dut.a.value = float_to_bin32(a)
        dut.b.value = float_to_bin32(b)

        # 等待几个时钟周期以获取输出
        await RisingEdge(dut.clk)
        await RisingEdge(dut.clk)

        # 获取输出
        result = int(dut.result.value)
        cocotb.log.info(f'{a} * {b} = {bin32_to_float(result)}')

        # 简单的验证逻辑（这里只是示例，需要根据实际情况修改）
        expected_result = float_to_bin32(a * b)  # 简单加法作为示例
        assert abs(bin32_to_float(result) - (a*b)) < 1e-6, \
            f"Test failed: {hex(float_to_bin32(a))} + {hex(float_to_bin32(b))} = {hex(result)}, expected {hex(expected_result)}"

    cocotb.log.info("All tests passed!")
