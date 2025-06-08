# Makefile

# defaults
SIM ?= icarus
TOPLEVEL_LANG ?= verilog
WAVES ?= 1

VERILOG_SOURCES += $(PWD)/matrix_vector_multiplier.v
VERILOG_SOURCES += $(PWD)/fp_multiplier.v
VERILOG_SOURCES += $(PWD)/fp_adder.v
VERILOG_SOURCES += $(PWD)/fp_adder_com.v
VERILOG_SOURCES += $(PWD)/fp_multiplier_com.v
# use VHDL_SOURCES for VHDL files

# TOPLEVEL is the name of the toplevel module in your Verilog or VHDL file
TOPLEVEL ?= matrix_vector_multiplier

# MODULE is the basename of the Python test file
MODULE ?= matrix_vector_multiplier

# include cocotb's make rules to take care of the simulator setup
include $(shell cocotb-config --makefiles)/Makefile.sim
