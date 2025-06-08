import mycocotb
from mycocotb.triggers import NullTrigger, Timer, RisingEdge, FallingEdge
import debugpy
debugpy.listen(4000)
print('Waiting for client...')
# debugpy.wait_for_client()

class Future():
    def __await__(self):
        yield

async def printa(dut):
    await RisingEdge(dut.a)
    print(f"after rising edge of dut.a = {dut.a.value}")

async def test(dut):
    print("Hello, World!")
    await NullTrigger()
    mycocotb.start_soon(printa(dut))
    print(f"dut.a = {dut.a.value}")
    await Timer(2)
    print(f"dut.a = {dut.a.value}")
    print('try to set dut.a ...')
    dut.a.value = ~dut.a.value
    await Timer(1)

mycocotb.start_soon(test(mycocotb.top))