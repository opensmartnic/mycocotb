from setuptools import setup, Extension
import sysconfig

# 获取 Python 包含目录
python_include = sysconfig.get_paths()['include']

# 定义扩展模块
simulator_module = Extension(
    'simulator',
    sources=['simulatormodule.cpp'],
    include_dirs=[python_include, "/usr/include/iverilog/"],
    # 实际上这个库是有对myvpi.vpl有外部符号依赖的，但因为它总是先于本库被加载，所以可以不写
    libraries=[],
    library_dirs=[]
)

setup(
    name='simulator',
    version='1.0',
    description='Python extension to provide access to the simulator',
    ext_modules=[simulator_module]
)