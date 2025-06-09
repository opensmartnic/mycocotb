# Copyright (c) 2013 Potential Ventures Ltd
# Copyright (c) 2013 SolarFlare Communications Inc
# All rights reserved.

import ast
import inspect
import logging
import os
import random
import sys
import time
import warnings
from collections.abc import Coroutine
from enum import auto, Enum
from types import SimpleNamespace
from typing import Any, Callable, Dict, List, Union, cast
from importlib import import_module

import mycocotb.handle
import mycocotb.task
import mycocotb.triggers
from mycocotb._scheduler import Scheduler
import mycocotb._write_scheduler
# from cocotb.logging import default_config
# 这里不使用cocotb.tests这样的注解，由用户直接用mycocotb.start_soon来创建协程
# from cocotb.regression import RegressionManager, RegressionMode


log: logging.Logger
"""The default cocotb logger."""

_scheduler_inst: Scheduler
"""The global scheduler instance."""

argv: List[str]
"""The argument list as seen by the simulator."""

SIM_NAME: str
"""The product information of the running simulator."""

SIM_VERSION: str
"""The version of the running simulator."""

# 这个全局变量可以用来在用户写的代码里获取DUT对象
top: mycocotb.handle.SimHandleBase
r"""
A handle to the :envvar:`COCOTB_TOPLEVEL` entity/module.

This is equivalent to the :term:`DUT` parameter given to cocotb tests, so it can be used wherever that variable can be used.
It is particularly useful for extracting information about the :term:`DUT` in module-level class and function definitions;
and in parameters to :class:`.TestFactory`\ s.
"""

is_simulation: bool = False
"""``True`` if cocotb was loaded in a simulation."""


class SimPhase(Enum):
    """A phase of the time step."""

    NORMAL = (auto(), "In the Beginning Of Time Step or a Value Change phase.")
    READ_WRITE = (auto(), "In a ReadWrite phase.")
    READ_ONLY = (auto(), "In a ReadOnly phase.")


sim_phase: SimPhase = SimPhase.NORMAL
"""The current phase of the time step."""


def _setup_logging() -> None:
    # default_config()
    global log
    # 这里cocotb原始的设置里，使用的仿真时间。我们这里简化处理，只采用主机时间
    logging.basicConfig(level = logging.INFO,format = '%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    log = logging.getLogger(__name__)


def _task_done_callback(task: "cocotb.task.Task[Any]") -> None:
    # if cancelled, do nothing
    if task.cancelled():
        return
    # if there's a Task awaiting this one, don't fail
    if task.complete in _scheduler_inst._trigger2tasks:
        return
    # if no failure, do nothing
    e = task.exception()
    if e is None:
        return
    # there was a failure and no one is watching, fail test
    else:
        task.log.error(f"Exception raised by this task: {e}")
        import sys
        sys.exit()


def start_soon(
    coro: "Union[cocotb.task.Task[cocotb.task.ResultType], Coroutine[Any, Any, cocotb.task.ResultType]]",
) -> "cocotb.task.Task[cocotb.task.ResultType]":
    """
    Schedule a coroutine to be run concurrently in a :class:`~cocotb.task.Task`.

    Note that this is not an ``async`` function,
    and the new task will not execute until the calling task yields control.

    Args:
        coro: A task or coroutine to be run.

    Returns:
        The :class:`~cocotb.task.Task` that is scheduled to be run.

    .. versionadded:: 1.6.0
    """
    task = create_task(coro)
    task._add_done_callback(_task_done_callback)
    _scheduler_inst._schedule_task(task)
    return task


async def start(
    coro: "Union[cocotb.task.Task[cocotb.task.ResultType], Coroutine[Any, Any, cocotb.task.ResultType]]",
) -> "cocotb.task.Task[cocotb.task.ResultType]":
    """
    Schedule a coroutine to be run concurrently, then yield control to allow pending tasks to execute.

    The calling task will resume execution before control is returned to the simulator.

    When the calling task resumes, the newly scheduled task may have completed,
    raised an Exception, or be pending on a :class:`~cocotb.triggers.Trigger`.

    Args:
        coro: A task or coroutine to be run.

    Returns:
        The :class:`~cocotb.task.Task` that has been scheduled and allowed to execute.

    .. versionadded:: 1.6.0
    """
    task = start_soon(coro)
    await mycocotb.triggers.NullTrigger()
    return task


def create_task(
    coro: "Union[cocotb.task.Task[cocotb.task.ResultType], Coroutine[Any, Any, cocotb.task.ResultType]]",
) -> "cocotb.task.Task[cocotb.task.ResultType]":
    """
    Construct a coroutine into a :class:`~cocotb.task.Task` without scheduling the task.

    The task can later be scheduled with :func:`cocotb.start` or :func:`cocotb.start_soon`.

    Args:
        coro: An existing task or a coroutine to be wrapped.

    Returns:
        Either the provided :class:`~cocotb.task.Task` or a new Task wrapping the coroutine.

    .. versionadded:: 1.6.0
    """
    if isinstance(coro, mycocotb.task.Task):
        return coro
    elif isinstance(coro, Coroutine):
        return mycocotb.task.Task(coro)
    elif inspect.iscoroutinefunction(coro):
        raise TypeError(
            f"Coroutine function {coro} should be called prior to being scheduled."
        )
    elif inspect.isasyncgen(coro):
        raise TypeError(
            f"{coro.__qualname__} is an async generator, not a coroutine. "
            "You likely used the yield keyword instead of await."
        )
    else:
        raise TypeError(
            f"Attempt to add an object of type {type(coro)} to the scheduler, "
            f"which isn't a coroutine: {coro!r}\n"
        )


def _initialise_testbench(argv_: List[str]) -> None:
    from mycocotb import simulator

    simulator.set_sim_event_callback(_sim_event)

    global is_simulation
    is_simulation = True

    global argv
    argv = argv_

    # sys.path normally includes "" (the current directory), but does not appear to when python is embedded.
    # Add it back because users expect to be able to import files in their test directory.
    sys.path.insert(0, "")

    _setup_logging()

    # From https://www.python.org/dev/peps/pep-0565/#recommended-filter-settings-for-test-runners
    # If the user doesn't want to see these, they can always change the global
    # warning settings in their test module.
    if not sys.warnoptions:
        warnings.simplefilter("default")

    global SIM_NAME, SIM_VERSION
    SIM_NAME = simulator.get_simulator_product().strip()
    SIM_VERSION = simulator.get_simulator_version().strip()

    log.info(f"Running on {SIM_NAME} version {SIM_VERSION}")

    # _process_packages()
    _setup_root_handle()

    # setup global scheduler system
    global _scheduler_inst
    # 这里原先的可以在一次测试结束后继续执行下一个测试
    # 我们进行了简化，假设用户将只进行一次测试，即给test_complete_cb赋一个空值
    _scheduler_inst = Scheduler(test_complete_cb=lambda: None)
    # 在这里启动_write_scheduler._do_writes()的后台服务，在这个服务里，会根据
    # 是否有写入请求，自动await 一次 ReadWrite，然后才真正触发写入
    start_soon(mycocotb._write_scheduler._do_writes())

    # 加载用户的python代码，假设用户会将自定义的协程通过mycocotb.start_soon注册到_scheduler_inst中
    load_user_code()
    
    # 启动事件循环。后续的事件循环触发则在trigger的响应函数（_sim_react）中
    _scheduler_inst._event_loop()

def load_user_code() -> None:
    # discover tests
    module_str = os.getenv("COCOTB_TEST_MODULES", "")
    if not module_str:
        raise RuntimeError(
            "Environment variable COCOTB_TEST_MODULES, which defines the module(s) to execute, is not defined or empty."
        )
    modules = [s.strip() for s in module_str.split(",") if s.strip()]
    for module_name in modules:
        mod = import_module(module_name)

def _sim_event(msg: str) -> None:
    """Function that can be called externally to signal an event."""
    # We simply return here as the simulator will exit
    # so no cleanup is needed
    if regression_manager is not None:
        regression_manager._fail_simulation(msg)
    else:
        log.error(msg)
    _shutdown_testbench()


def _process_packages() -> None:
    # 这里去掉了对package的支持
    pass


def _setup_root_handle() -> None:
    root_name = os.getenv("COCOTB_TOPLEVEL")
    if root_name is not None:
        root_name = root_name.strip()
        if root_name == "":
            root_name = None
        elif "." in root_name:
            # Skip any library component of the toplevel
            root_name = root_name.split(".", 1)[1]

    from mycocotb import simulator
    handle = simulator.get_root_handle(root_name)
    if not handle:
        raise RuntimeError(f"Can not find root handle {root_name!r}")

    global top
    top = mycocotb.handle.SimHandle(handle)

