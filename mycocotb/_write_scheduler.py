# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
import os
from collections import OrderedDict
from typing import Any, Callable, Sequence, Tuple, Union

import mycocotb
import mycocotb.handle
import mycocotb.task
from mycocotb.triggers import Event, ReadWrite

# A dictionary of pending (write_func, args), keyed by handle.
# Writes are applied oldest to newest (least recently used).
# Only the last scheduled write to a particular handle in a timestep is performed.
_write_calls: "OrderedDict[cocotb.handle.SimHandleBase, Tuple[Callable[..., None], Sequence[Any]]]" = OrderedDict()

# TODO don't use a task to force ReadWrite, just prime an empty callback

_write_task: Union[mycocotb.task.Task[None], None] = None

_writes_pending = Event()


async def _do_writes() -> None:
    """An internal task that schedules a ReadWrite to force writes to occur."""
    while True:
        await _writes_pending.wait()
        await ReadWrite()


def start_write_scheduler() -> None:
    global _write_task
    if _write_task is None:
        _write_task = mycocotb.start_soon(_do_writes())


def stop_write_scheduler() -> None:
    global _write_task
    if _write_task is not None:
        _write_task.kill()
        _write_task = None
    _write_calls.clear()
    _writes_pending.clear()


def apply_scheduled_writes() -> None:
    while _write_calls:
        _, (func, args) = _write_calls.popitem(last=False)
        func(*args)
    _writes_pending.clear()


def schedule_write(
    handle: mycocotb.handle.SimHandleBase,
    write_func: Callable[..., None],
    args: Sequence[Any],
) -> None:
    """Queue *write_func* to be called on the next ``ReadWrite`` trigger."""
    if mycocotb.sim_phase == mycocotb.SimPhase.READ_WRITE:
        write_func(*args)
    elif mycocotb.sim_phase == mycocotb.SimPhase.READ_ONLY:
        raise RuntimeError(
            f"Write to object {handle._name} was scheduled during a read-only simulation phase."
        )
    else:
        if handle in _write_calls:
            del _write_calls[handle]
        _write_calls[handle] = (write_func, args)
        _writes_pending.set()
