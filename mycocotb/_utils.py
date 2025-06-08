# Copyright (c) 2013 Potential Ventures Ltd
# Copyright (c) 2013 SolarFlare Communications Inc
# All rights reserved.

"""Utilities for implementors."""

import inspect
import os
import sys
import traceback
import types
import weakref
from enum import Enum
from functools import lru_cache, update_wrapper, wraps
from types import TracebackType
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    Dict,
    Iterable,
    List,
    Optional,
    Sequence,
    Tuple,
    Type,
    TypeVar,
    Union,
    cast,
    overload,
)

ExceptionTuple = Tuple[
    Type[BaseException], BaseException, TracebackType
]  # TypeAlias in Python 3.10


@overload
def remove_traceback_frames(
    tb_or_exc: ExceptionTuple, frame_names: List[str]
) -> ExceptionTuple: ...


@overload
def remove_traceback_frames(
    tb_or_exc: BaseException, frame_names: List[str]
) -> BaseException: ...


@overload
def remove_traceback_frames(
    tb_or_exc: TracebackType, frame_names: List[str]
) -> TracebackType: ...


def remove_traceback_frames(
    tb_or_exc: Union[ExceptionTuple, BaseException, TracebackType],
    frame_names: List[str],
) -> Union[ExceptionTuple, BaseException, TracebackType]:
    """
    Strip leading frames from a traceback

    Args:
        tb_or_exc:
            Object to strip frames from. If an exception is passed, creates
            a copy of the exception with a new shorter traceback. If a tuple
            from `sys.exc_info` is passed, returns the same tuple with the
            traceback shortened
        frame_names:
            Names of the frames to strip, which must be present at the top of the Traceback or Exception.

    Returns:
        Traceback or Exception passed to the function with the *frame_names* stripped out.
    """
    # self-invoking overloads
    if isinstance(tb_or_exc, BaseException):
        exc: BaseException = tb_or_exc
        return exc.with_traceback(
            remove_traceback_frames(cast(TracebackType, exc.__traceback__), frame_names)
        )
    elif isinstance(tb_or_exc, tuple):
        exc_type, exc_value, exc_tb = cast(ExceptionTuple, tb_or_exc)
        exc_tb = remove_traceback_frames(exc_tb, frame_names)
        return exc_type, exc_value, exc_tb
    # base case
    else:
        tb: TracebackType = tb_or_exc
        for frame_name in frame_names:
            # the assert and cast are there assuming the frame_names being removed are correct
            assert tb.tb_frame.f_code.co_name == frame_name
            tb = cast(TracebackType, tb.tb_next)
        return tb


def walk_coro_stack(
    coro: "types.CoroutineType[Any, Any, Any]",
) -> Iterable[Tuple[types.FrameType, int]]:
    """Walk down the coroutine stack, starting at *coro*.

    Args:
        coro: The :class:`coroutine` object to traverse.

    Yields:
        Frame and line number of each frame in the coroutine.
    """
    c: Optional[types.CoroutineType[Any, Any, Any]] = coro
    while c is not None:
        try:
            f = c.cr_frame
        except AttributeError:
            break
        else:
            c = c.cr_await
        if f is not None:
            yield (f, f.f_lineno)


def extract_coro_stack(
    coro: "types.CoroutineType[Any, Any, Any]", limit: Optional[int] = None
) -> traceback.StackSummary:
    r"""Create a list of pre-processed entries from the coroutine stack.

    This is based on :func:`traceback.extract_tb`.

    If *limit* is omitted or ``None``, all entries are extracted.
    The list is a :class:`traceback.StackSummary` object, and
    each entry in the list is a :class:`traceback.FrameSummary` object
    containing attributes ``filename``, ``lineno``, ``name``, and ``line``
    representing the information that is usually printed for a stack
    trace. The line is a string with leading and trailing
    whitespace stripped; if the source is not available it is ``None``.

    Args:
        coro: The :class:`coroutine` object from which to extract a stack.
        level: The maximum number of frames from *coro*\ s stack to extract.

    Returns:
        The stack of *coro*.
    """
    return traceback.StackSummary.extract(walk_coro_stack(coro), limit=limit)


EnumT = TypeVar("EnumT", bound=Enum)


class DocEnum(Enum):
    """Like :class:`enum.Enum`, but allows documenting enum values.

    Documentation for enum members can be optionally added by setting enum values to a tuple of the intended value and the docstring.
    This adds the provided docstring to the ``__doc__`` field of the enum value.

    .. code-block:: python3

        class MyEnum(DocEnum):
            \"\"\"Class documentation\"\"\"

            VALUE1 = 1, "Value documentation"
            VALUE2 = 2  # no documentation

    Taken from :ref:`this StackOverflow answer <https://stackoverflow.com/questions/50473951/how-can-i-attach-documentation-to-members-of-a-python-enum/50473952#50473952>`
    by :ref:`Eric Wieser <https://stackoverflow.com/users/102441/eric>`,
    as recommended by the ``enum_tools`` documentation.
    """

    def __new__(cls: Type[EnumT], value: Any, doc: Optional[str] = None) -> EnumT:
        # super().__new__() assumes the value is already an enum value
        # so we side step that and create a raw object and fill in _value_
        self = object.__new__(cls)
        self._value_ = value
        if doc is not None:
            self.__doc__ = doc
        return self


if TYPE_CHECKING:  # pragma: no cover
    F = TypeVar("F")

    def cached_method(f: F) -> F: ...

else:

    class cached_method:
        def __init__(self, method):
            self._method = method
            update_wrapper(self, method)

        def __get__(self, instance, objtype=None):
            if instance is None:
                return self

            cache = {}

            @wraps(self._method)
            def lookup(*args, **kwargs):
                key = (args, tuple(kwargs.items()))
                try:
                    return cache[key]
                except KeyError:
                    res = self._method(instance, *args, **kwargs)
                    cache[key] = res
                    return res

            lookup.cache = cache

            setattr(instance, self._method.__name__, lookup)
            return lookup


_T = TypeVar("_T", bound=type)
_Value = TypeVar("_Value", bound=object)


def singleton(orig_cls: _T) -> _T:
    """Class decorator which turns a type into a Singleton type."""
    orig_new = orig_cls.__new__
    orig_init = orig_cls.__init__
    instance = None

    @wraps(orig_cls.__new__)
    def __new__(cls: Type[_Value], *args: Any, **kwargs: Any) -> _Value:
        nonlocal instance
        if instance is None:
            instance = orig_new(cls, *args, **kwargs)
            orig_init(instance, *args, **kwargs)
        return instance

    @wraps(orig_cls.__init__)
    def __init__(self, *args: Any, **kwargs: Any) -> None:
        pass

    orig_cls.__new__ = __new__
    orig_cls.__init__ = __init__
    return orig_cls
