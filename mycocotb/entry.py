import sys
import mycocotb

def load_entry(argv):
    print(argv)
    print(sys.path)
    mycocotb._initialise_testbench(argv)