CC = gcc
CFLAGS = -g -fPIC -shared -lvpi -I/usr/include/iverilog/
V_TARGET = build/sim
C_TARGET = build/myvpi.vpl
C_TARGET_NO_EXT = myvpi
V_SRC = $(wildcard tests/*.v)
C_SRC = VpiImpl.cpp VpiObj.cpp GpiCommon.cpp
C_TO_PY_SRC = simulatormodule.cpp
H_SRC = $(wildcard *.h)
PY_INCLUDE = $(shell python3-config --includes)
PY_LDFLAGS = $(shell python3-config --ldflags --embed)

# mycocotb运行需要的环境变量
COCOTB_TEST_TOPLEVEL ?= dut
COCOTB_TEST_MODULES ?= tests.mytest
COCOTB_RUN_TOPLEVEL ?= matrix_vector_multiplier
COCOTB_RUN_MODULES ?= tests.matrix_vector_multiplier_mycocotb

all: $(V_TARGET) $(C_TARGET)

test: all
	PYGPI_PYTHON_BIN=$(shell which python3) \
	COCOTB_TOPLEVEL=$(COCOTB_TEST_TOPLEVEL) \
	COCOTB_TEST_MODULES=$(COCOTB_TEST_MODULES) \
	/usr/bin/vvp -M./build -m$(C_TARGET_NO_EXT) $(V_TARGET)

run: all
	PYGPI_PYTHON_BIN=$(shell which python3) \
	COCOTB_TOPLEVEL=$(COCOTB_RUN_TOPLEVEL) \
	COCOTB_TEST_MODULES=$(COCOTB_RUN_MODULES) \
	/usr/bin/vvp -M./build -m$(C_TARGET_NO_EXT) $(V_TARGET)

$(V_TARGET): $(V_SRC)
	iverilog -g2012 $^ -o $@

$(C_TARGET): $(C_SRC) $(H_SRC) $(C_TO_PY_SRC)
	$(CC) -o $@  $(CFLAGS) $(PY_INCLUDE) $(C_SRC)  $(PY_LDFLAGS)
	python setup.py build
	cp build/lib.linux*/*.so ./mycocotb

clean:
	# rm -f $(C_TARGET) $(V_TARGET)
	rm mycocotb/*.so
	rm -rf build/*

.PHONY: all run clean test