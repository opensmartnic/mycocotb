# Copyright (c) 2013 Potential Ventures Ltd
# Copyright (c) 2013 SolarFlare Communications Inc
# All rights reserved.

"""A collection of triggers which a testbench can ``await``."""

import logging
import warnings
from abc import abstractmethod
from decimal import Decimal
from fractions import Fraction
from typing import (
    TYPE_CHECKING,
    Any,
    AsyncContextManager,
    Awaitable,
    Callable,
    ClassVar,
    Coroutine,
    Generator,
    Generic,
    List,
    Optional,
    Type,
    TypeVar,
    Union,
    cast,
    overload,
)

import mycocotb.handle
import mycocotb.task
from mycocotb import simulator
from mycocotb._outcomes import Error, Outcome, Value
from functools import cached_property
from mycocotb._utils import (
    singleton,
)
from mycocotb.utils import get_sim_steps, get_time_from_sim_steps

T = TypeVar("T")


def _pointer_str(obj: object) -> str:
    """Get the memory address of *obj* as used in :meth:`object.__repr__`.

    This is equivalent to ``sprintf("%p", id(obj))``, but python does not
    support ``%p``.
    """
    full_repr = object.__repr__(obj)  # gives "<{type} object at {address}>"
    return full_repr.rsplit(" ", 1)[1][:-1]


Self = TypeVar("Self", bound="Trigger")


class Trigger(Awaitable["Trigger"]):
    """Base class to derive from."""

    def __init__(self) -> None:
        self._primed = False

    @cached_property
    def log(self) -> logging.Logger:
        """A :class:`logging.Logger` for the trigger."""
        return logging.getLogger(f"cocotb.{type(self).__qualname__}.0x{id(self):x}")

    def _prime(self, callback: Callable[["Trigger"], None]) -> None:
        """Set a callback to be invoked when the trigger fires.

        The callback will be invoked with a single argument, `self`.

        Sub-classes must override this, but should end by calling the base class
        method.

        .. warning::
            Do not call this directly within a :term:`task`. It is intended to be used
            only by the scheduler.
        """
        self._primed = True

    def _unprime(self) -> None:
        """Remove the callback, and perform cleanup if necessary.

        After being un-primed, a Trigger may be re-primed again in the future.
        Calling `_unprime` multiple times is allowed, subsequent calls should be
        a no-op.

        Sub-classes may override this, but should end by calling the base class
        method.

        .. warning::
            Do not call this directly within a :term:`task`. It is intended to be used
            only by the scheduler.
        """
        self._cleanup()

    def _cleanup(self) -> None:
        self._primed = False

    def __await__(self: Self) -> Generator[Self, None, Self]:
        yield self
        return self


class GPITrigger(Trigger):
    """Base Trigger class for GPI triggers.

    Consumes simulation time.
    """

    def __init__(self) -> None:
        super().__init__()

        # Required to ensure documentation can build
        # if simulator is not None:
        #    self.cbhdl = simulator.create_callback(self)
        # else:
        self._cbhdl: Optional[simulator.gpi_cb_hdl] = None

    def _unprime(self) -> None:
        """Disable a primed trigger, can be re-primed."""
        if self._cbhdl is not None:
            self._cbhdl.deregister()
        return super()._unprime()

    def _cleanup(self) -> None:
        self._cbhdl = None
        return super()._cleanup()


class Timer(GPITrigger):
    r"""Fire after the specified simulation time period has elapsed.

    This trigger will *always* consume some simulation time
    and will return control to the ``await``\ ing task at the beginning of the time step.

    Args:
        time: The time value.

            .. versionchanged:: 1.5.0
                Previously this argument was misleadingly called `time_ps`.

        units: The unit of the time value.

            One of ``'step'``, ``'fs'``, ``'ps'``, ``'ns'``, ``'us'``, ``'ms'``, ``'sec'``.
            When *units* is ``'step'``,
            the timestep is determined by the simulator (see :make:var:`COCOTB_HDL_TIMEPRECISION`).

        round_mode:

            String specifying how to handle time values that sit between time steps
            (one of ``'error'``, ``'round'``, ``'ceil'``, ``'floor'``).

    Raises:
        ValueError: If a non-positive value is passed for Timer setup.

    Usage:

        >>> await Timer(100, units="ps")

        The time can also be a ``float``:

        >>> await Timer(100e-9, units="sec")

        which is particularly convenient when working with frequencies:

        >>> freq = 10e6  # 10 MHz
        >>> await Timer(1 / freq, units="sec")

        Other builtin exact numeric types can be used too:

        >>> from fractions import Fraction
        >>> await Timer(Fraction(1, 10), units="ns")

        >>> from decimal import Decimal
        >>> await Timer(Decimal("100e-9"), units="sec")

        These are most useful when using computed durations while
        avoiding floating point inaccuracies.

    .. versionchanged:: 1.5
        Raise an exception when Timer uses a negative value as it is undefined behavior.
        Warn for 0 as this will cause erratic behavior in some simulators as well.

    .. versionchanged:: 1.5
        Support ``'step'`` as the *units* argument to mean "simulator time step".

    .. versionchanged:: 1.6
        Support rounding modes.

    .. versionremoved:: 2.0
        Passing ``None`` as the *units* argument was removed, use ``'step'`` instead.

    .. versionremoved:: 2.0
        The ``time_ps`` parameter was removed, use the ``time`` parameter instead.

    .. versionchanged:: 2.0
        Passing ``0`` as the *time* argument now raises a :exc:`ValueError`.
    """

    round_mode: str = "error"
    """The default rounding mode."""

    def __init__(
        self,
        time: Union[float, Fraction, Decimal],
        units: str = "step",
        *,
        round_mode: Optional[str] = None,
    ) -> None:
        super().__init__()
        if time <= 0:
            raise ValueError("Timer argument time must be positive")
        if round_mode is None:
            round_mode = type(self).round_mode
        self._sim_steps = get_sim_steps(time, units, round_mode=round_mode)
        # If we round to 0, we fix it up to 1 step as rounding is imprecise,
        # and Timer(0) is invalid.
        if self._sim_steps == 0:
            self._sim_steps = 1

    def _prime(self, callback: Callable[[Trigger], None]) -> None:
        """Register for a timed callback."""
        if self._cbhdl is None:
            self._cbhdl = simulator.register_timed_callback(
                self._sim_steps, callback, self
            )
            if self._cbhdl is None:
                raise RuntimeError(f"Unable set up {str(self)} Trigger")
        super()._prime(callback)

    def __repr__(self) -> str:
        return "<{} of {:1.2f}ps at {}>".format(
            type(self).__qualname__,
            get_time_from_sim_steps(self._sim_steps, units="ps"),
            _pointer_str(self),
        )


@singleton
class ReadOnly(GPITrigger):
    """Fires when the current simulation timestep moves to the read-only phase.

    The read-only phase is entered when the current timestep no longer has any further delta steps.
    This will be a point where all the signal values are stable as there are no more RTL events scheduled for the timestep.
    The simulator will not allow scheduling of more events in this timestep.
    Useful for monitors which need to wait for all processes to execute (both RTL and cocotb) to ensure sampled signal values are final.
    """

    def _prime(self, callback: Callable[[Trigger], None]) -> None:
        if mycocotb.sim_phase is mycocotb.SimPhase.READ_ONLY:
            raise RuntimeError(
                "Attempted illegal transition: awaiting ReadOnly in ReadOnly phase"
            )
        if self._cbhdl is None:
            self._cbhdl = simulator.register_readonly_callback(callback, self)
            if self._cbhdl is None:
                raise RuntimeError(f"Unable set up {str(self)} Trigger")
        super()._prime(callback)

    def __repr__(self) -> str:
        return f"{type(self).__qualname__}()"


@singleton
class ReadWrite(GPITrigger):
    """Fires when the read-write simulation phase is reached."""

    def _prime(self, callback: Callable[[Trigger], None]) -> None:
        if mycocotb.sim_phase is mycocotb.SimPhase.READ_ONLY:
            raise RuntimeError(
                "Attempted illegal transition: awaiting ReadWrite in ReadOnly phase"
            )
        if self._cbhdl is None:
            self._cbhdl = simulator.register_rwsynch_callback(callback, self)
            if self._cbhdl is None:
                raise RuntimeError(f"Unable set up {str(self)} Trigger")
        super()._prime(callback)

    def __repr__(self) -> str:
        return f"{type(self).__qualname__}()"


@singleton
class NextTimeStep(GPITrigger):
    """Fires when the next time step is started."""

    def _prime(self, callback: Callable[[Trigger], None]) -> None:
        if self._cbhdl is None:
            self._cbhdl = simulator.register_nextstep_callback(callback, self)
            if self._cbhdl is None:
                raise RuntimeError(f"Unable set up {str(self)} Trigger")
        super()._prime(callback)

    def __repr__(self) -> str:
        return f"{type(self).__qualname__}()"


class _EdgeBase(GPITrigger):
    """Internal base class that fires on a given edge of a signal."""

    _edge_type: ClassVar[int]

    def __init__(self, signal: mycocotb.handle.ValueObjectBase[Any, Any]) -> None:
        super().__init__()
        self.signal = signal

    def _prime(self, callback: Callable[[Trigger], None]) -> None:
        if self._cbhdl is None:
            self._cbhdl = simulator.register_value_change_callback(
                self.signal._handle, callback, type(self)._edge_type, self
            )
            if self._cbhdl is None:
                raise RuntimeError(f"Unable set up {str(self)} Trigger")
        super()._prime(callback)

    def __repr__(self) -> str:
        return f"{type(self).__qualname__}({self.signal!r})"


class RisingEdge(_EdgeBase):
    """Fires on the rising edge of *signal*, on a transition to ``1``.

    Only valid for scalar ``logic`` or ``bit``-typed signals.

    Args:
        signal: The signal upon which to wait for a rising edge.

    Raises:
        TypeError: If *signal* is not a 1-bit ``logic`` or ``bit``-typed object.

    .. warning::
        On many simulators this will trigger on transitions from non-``0``/``1`` value to ``1``,
        not just from ``0`` to ``1`` like the ``rising_edge`` function in VHDL.
    """

    _edge_type = simulator.RISING

    @classmethod
    def __singleton_key__(
        cls, signal: mycocotb.handle.LogicObject
    ) -> mycocotb.handle.LogicObject:
        if not (isinstance(signal, mycocotb.handle.LogicObject)):
            raise TypeError(
                f"{cls.__qualname__} requires a scalar LogicObject. Got {signal!r} of type {type(signal).__qualname__}"
            )
        return signal


class FallingEdge(_EdgeBase):
    """Fires on the falling edge of *signal*, on a transition to ``0``.

    Only valid for scalar ``logic`` or ``bit``-typed signals.

    Args:
        signal: The signal upon which to wait for a rising edge.

    Raises:
        TypeError: If *signal* is not a 1-bit ``logic`` or ``bit``-typed object.

    .. warning::
        On many simulators this will trigger on transitions from non-``0``/``1`` value to ``0``,
        not just from ``1`` to ``0`` like the ``falling_edge`` function in VHDL.
    """

    _edge_type = simulator.FALLING

    @classmethod
    def __singleton_key__(
        cls, signal: mycocotb.handle.LogicObject
    ) -> mycocotb.handle.LogicObject:
        if not (isinstance(signal, mycocotb.handle.LogicObject)):
            raise TypeError(
                f"{cls.__qualname__} requires a scalar LogicObject. Got {signal!r} of type {type(signal).__qualname__}"
            )
        return signal


class Edge(_EdgeBase):
    """Fires on any value change of *signal*.

    Args:
        signal: The signal upon which to wait for a value change.

    Raises:
        TypeError: If the signal is not an object which can change value.
    """

    _edge_type = simulator.VALUE_CHANGE

    @classmethod
    def __singleton_key__(
        cls, signal: mycocotb.handle.ValueObjectBase[Any, Any]
    ) -> mycocotb.handle.ValueObjectBase[Any, Any]:
        if not isinstance(signal, mycocotb.handle.ValueObjectBase):
            raise TypeError(
                f"{cls.__qualname__} requires an object derived from ValueObjectBase which can change value. Got {signal!r} of type {type(signal).__qualname__}"
            )
        return signal


class _Event(Trigger):
    """Unique instance used by the Event object.

    One created for each attempt to wait on the event so that the scheduler
    can maintain a unique mapping of triggers to tasks.
    """

    def __init__(self, parent: "Event") -> None:
        super().__init__()
        self._parent = parent

    def _prime(self, callback: Callable[[Trigger], None]) -> None:
        if self._primed:
            return
        self._callback = callback
        self._parent._prime_trigger(self, callback)
        return super()._prime(callback)

    def _unprime(self) -> None:
        if not self._primed:
            return
        self._parent._unprime_trigger(self)
        return super()._unprime()

    def __repr__(self) -> str:
        return f"<{self._parent!r}.wait() at {_pointer_str(self)}>"


class Event:
    r"""A way to signal an event across :class:`~cocotb.task.Task`\ s.

    ``await``\ ing the result of :meth:`wait()` will block the ``await``\ ing :class:`~cocotb.task.Task`
    until :meth:`set` is called.

    Args:
        name: Name for the Event.

    Usage:

        .. code-block:: python3

            e = Event()


            async def task1():
                await e.wait()
                print("resuming!")


            cocotb.start_soon(task1())
            # do stuff
            e.set()
            await NullTrigger()  # allows task1 to execute
            # resuming!

    .. versionremoved:: 2.0

        Removed the undocumented *data* attribute and argument to :meth:`set`.
    """

    def __init__(self, name: Optional[str] = None) -> None:
        self._pending_events: List[_Event] = []
        self.name: Optional[str] = name
        self._fired: bool = False
        self._data: Any = None

    def _prime_trigger(
        self, trigger: _Event, callback: Callable[[Trigger], None]
    ) -> None:
        self._pending_events.append(trigger)

    def _unprime_trigger(self, trigger: _Event) -> None:
        self._pending_events.remove(trigger)

    def set(self, data: Optional[Any] = None) -> None:
        """Set the Event and unblock all Tasks blocked on this Event."""
        self._fired = True
        if data is not None:
            warnings.warn(
                "The data field will be removed in a future release.",
                DeprecationWarning,
            )
        self._data = data

        pending_events, self._pending_events = self._pending_events, []
        for event in pending_events:
            event._callback(event)

    def wait(self) -> Trigger:
        """Block the current Task until the Event is set.

        If the event has already been set, the trigger will fire immediately.

        To set the Event call :meth:`set`.
        To reset the Event (and enable the use of :meth:`wait` again),
        call :meth:`clear`.
        """
        if self._fired:
            return NullTrigger(name=f"{str(self)}.wait()")
        return _Event(self)

    def clear(self) -> None:
        """Clear this event that has been set.

        Subsequent calls to :meth:`~cocotb.triggers.Event.wait` will block until
        :meth:`~cocotb.triggers.Event.set` is called again.
        """
        self._fired = False

    def is_set(self) -> bool:
        """Return ``True`` if event has been set."""
        return self._fired

    def __repr__(self) -> str:
        if self.name is None:
            fmt = "<{0} at {2}>"
        else:
            fmt = "<{0} for {1} at {2}>"
        return fmt.format(type(self).__qualname__, self.name, _pointer_str(self))


class NullTrigger(Trigger):
    """Fires immediately.

    This is primarily for forcing the current Task to be rescheduled after all currently pending Tasks.

    .. versionremoved:: 2.0
        The *outcome* parameter was removed. There is no alternative.
    """

    def __init__(self, name: Optional[str] = None) -> None:
        super().__init__()
        self.name = name

    def _prime(self, callback: Callable[[Trigger], None]) -> None:
        callback(self)

    def __repr__(self) -> str:
        if self.name is None:
            fmt = "<{0} at {2}>"
        else:
            fmt = "<{0} for {1} at {2}>"
        return fmt.format(type(self).__qualname__, self.name, _pointer_str(self))


class TaskComplete(Trigger, Generic[T]):
    r"""Fires when a :class:`~cocotb.task.Task` completes.

    Unlike :func:`~cocotb.triggers.Join`, this Trigger does not return the result of the Task when ``await``\ ed.

    .. note::
        It is preferable to use :attr:`.Task.complete` to get this object over calling the constructor.

    .. code-block:: python3

        async def coro_inner():
            await Timer(1, units="ns")
            raise ValueError("Oops")


        task = cocotb.start_soon(coro_inner())
        await task.complete  # no exception raised here
        assert task.exception() == ValueError("Oops")

    Args:
        task: The Task upon which to wait for completion.

    .. versionadded: 2.0
    """

    def __new__(cls, task: "cocotb.task.Task[T]") -> "TaskComplete[T]":
        return task.complete

    @classmethod
    def _make(cls, task: "cocotb.task.Task[T]") -> "TaskComplete[T]":
        self = super().__new__(cls)
        cls.__init__(self, task)
        return self

    def __init__(self, task: "cocotb.task.Task[T]") -> None:
        super().__init__()
        self._task = task

    def _prime(self, callback: Callable[[Trigger], None]) -> None:
        if self._task.done():
            callback(self)
        else:
            super()._prime(callback)

    def __repr__(self) -> str:
        return f"{type(self).__qualname__}({self._task!s})"

    @property
    def task(self) -> "cocotb.task.Task[T]":
        """The :class:`.Task` associated with this completion event."""
        return self._task

