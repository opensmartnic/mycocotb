#!/usr/bin/env python

# Copyright (c) 2013, 2018 Potential Ventures Ltd
# Copyright (c) 2013 SolarFlare Communications Inc
# All rights reserved.

"""Task scheduler.

FIXME: We have a problem here. If a task schedules a read-only but we
also have pending writes we have to schedule the ReadWrite callback before
the ReadOnly (and this is invalid, at least in Modelsim).
"""

import logging
import os
import threading
from collections import OrderedDict
from typing import Any, Callable, Dict

import mycocotb
import mycocotb._write_scheduler
from mycocotb import _outcomes
from mycocotb.task import Task
from mycocotb.triggers import (
    Event,
    GPITrigger,
    NextTimeStep,
    ReadOnly,
    ReadWrite,
    Trigger,
)

# Sadly the Python standard logging module is very slow so it's better not to
# make any calls by testing a boolean flag first
# _debug = "COCOTB_SCHEDULER_DEBUG" in os.environ
_debug = False


class external_state:
    INIT = 0
    RUNNING = 1
    PAUSED = 2
    EXITED = 3

class Scheduler:
    """The main Task scheduler.

    How It Generally Works:
        Tasks are `queued` to run in the scheduler with :meth:`_queue`.
        Queueing adds the Task and an Outcome value to :attr:`_pending_tasks`.
        The main scheduling loop is located in :meth:`_event_loop` and loops over the queued Tasks and `schedules` them.
        :meth:`_schedule` schedules a Task -
        continuing its execution from where it previously yielded control -
        by injecting the Outcome value associated with the Task from the queue.
        The Task's body will run until it finishes or reaches the next ``await`` statement.
        If a Task reaches an ``await``, :meth:`_schedule` will convert the value yielded from the Task into a Trigger with :meth:`_trigger_from_any` and its friend methods.
        Triggers are then `primed` (with :meth:`~cocotb.triggers.Trigger._prime`)
        with a `react` function (:meth:`_sim_react` or :meth:`_react)
        so as to wake up Tasks waiting for that Trigger to `fire` (when the event encoded by the Trigger occurs).
        This is accomplished by :meth:`_resume_task_upon`.
        :meth:`_resume_task_upon` also associates the Trigger with the Task waiting on it to fire by adding them to the :attr:`_trigger2tasks` map.
        If, instead of reaching an ``await``, a Task finishes, :meth:`_schedule` will cause the :class:`~cocotb.triggers.Join` trigger to fire.
        Once a Trigger fires it calls the react function which queues all Tasks waiting for that Trigger to fire.
        Then the process repeats.

        When a Task is cancelled (:meth:`_unschedule`), it is removed from the Task queue if it is currently queued.
        Also, the Task and Trigger are deassociated in the :attr:`_trigger2tasks` map.
        If the cancelled Task is the last Task waiting on a Trigger, that Trigger is `unprimed` to prevent it from firing.

    Simulator Phases:
        All GPITriggers (triggers that are fired by the simulator) go through :meth:`_sim_react`
        which looks at the fired GPITriggers to determine and track the current simulator phase cocotb is executing in.

        Normal phase:
            Corresponds to all non-ReadWrite and non-ReadOnly phases.
            Any writes are cached for the next ReadWrite phase and do not happen immediately.
            Scheduling :class:`~cocotb.triggers.ReadWrite` and :class:`~cocotb.triggers.ReadOnly` are valid.

        ReadWrite phase:
            Corresponds to ``cbReadWriteSynch`` (VPI) or ``vhpiCbRepLastKnownDeltaCycle`` (VHPI).
            At the start of scheduling in this phase we play back all the *previously* cached write updates.
            Any writes are cached for the next ReadWrite phase and do not happen immediately.
            Scheduling :class:`~cocotb.triggers.ReadWrite` and :class:`~cocotb.triggers.ReadOnly` are valid.
            One caveat is that scheduling a :class:`~cocotb.triggers.ReadWrite` while in this phase may not be valid.
            If there were no writes applied at the beginning of this phase, there will be no more events in this time step,
            and there will not be another ReadWrite phase in this time step.
            Simulators generally handle this caveat gracefully by leaving you in the ReadWrite phase of the next time step.

        ReadOnly phase
            Corresponds to ``cbReadOnlySynch`` (VPI) or ``vhpiCbRepEndOfTimeStep`` (VHPI).
            In this state we are not allowed to perform writes.
            Scheduling :class:`~cocotb.triggers.ReadWrite` and :class:`~cocotb.triggers.ReadOnly` are *not* valid.

    Caveats and Special Cases:
        The scheduler treats Tests specially.
        If a Test finishes or a Task ends with an Exception, the scheduler is put into a `terminating` state.
        All currently queued Tasks are cancelled and all pending Triggers are unprimed.
        This is currently spread out between :meth:`_handle_termination` and :meth:`_cleanup`.
        In that mix of functions, the :attr:`_test_complete_cb` callback is called to inform whomever (the regression_manager) the test finished.
        The scheduler also is responsible for starting the next Test in the Normal phase by priming a ``Timer(1)`` with the second half of test completion handling.

        The scheduler is currently where simulator time phase is tracked.
        This is mostly because this is where :meth:`_sim_react` is most conveniently located.
        The scheduler can't currently be made independent of simulator-specific code because of the above special cases which have to respect simulator phasing.

        Currently Task cancellation is accomplished with :meth:`Task.kill() <cocotb.task.Task.kill>`.
        This function immediately cancels the Task by re-entering the scheduler.
        This can cause issues if you are trying to cancel the Test Task or the currently executing Task.

        TODO: There are attributes and methods for dealing with "externals", but I'm not quite sure how it all works yet.
    """

    # Singleton events, recycled to avoid spurious object creation
    _next_time_step = NextTimeStep()
    _read_write = ReadWrite()
    _read_only = ReadOnly()
    _none_outcome = _outcomes.Value(None)

    def __init__(self, test_complete_cb: Callable[[], None]) -> None:
        self._test_complete_cb = test_complete_cb

        self.log = logging.getLogger("mycocotb.scheduler")
        if _debug:
            self.log.setLevel(logging.DEBUG)

        # A dictionary of pending tasks for each trigger,
        # indexed by trigger
        self._trigger2tasks: Dict[Trigger, list[Task]] = (
            dict()
        )

        self._scheduled_tasks: OrderedDict[Task[Any], _outcomes.Outcome] = OrderedDict()
        self._pending_threads = []
        self._pending_events = []  # Events we need to call set on once we've unwound

        self._terminate = False
        self._main_thread = threading.current_thread()

        self._current_task = None

    def _handle_termination(self) -> None:
        """
        Handle a termination that causes us to move onto the next test.
        """
        if _debug:
            self.log.debug("Scheduler terminating...")

        # cleanup triggers and tasks
        self._cleanup()

        # clear state
        self._terminate = False

        # call complete cb, may schedule another test
        self._test_complete_cb()

    def _sim_react(self, trigger: Trigger) -> None:
        """Called when a :class:`~cocotb.triggers.GPITrigger` fires.

        This is often the entry point into Python from the simulator,
        so this function is in charge of enabling profiling.
        It must also track the current simulator time phase,
        and start the unstarted event loop.
        """
        # TODO: move state tracking to global variable
        # and handle this via some kind of trigger-specific Python callback
        if trigger is self._read_write:
            mycocotb.sim_phase = mycocotb.SimPhase.READ_WRITE
        elif trigger is self._read_only:
            mycocotb.sim_phase = mycocotb.SimPhase.READ_ONLY
        else:
            mycocotb.sim_phase = mycocotb.SimPhase.NORMAL
        # apply inertial writes if ReadWrite
        if trigger is self._read_write:
            mycocotb._write_scheduler.apply_scheduled_writes()
        self._react(trigger)
        self._event_loop()

    def _react(self, trigger: Trigger) -> None:
        """Called when a :class:`~cocotb.triggers.Trigger` fires.

        Finds all Tasks waiting on the Trigger that fired and queues them.
        """
        if _debug:
            self.log.debug(f"Trigger fired: {trigger}")

        # find all tasks waiting on trigger that fired
        try:
            scheduling = self._trigger2tasks.pop(trigger)
        except KeyError:
            # GPI triggers should only be ever pending if there is an
            # associated task waiting on that trigger, otherwise it would
            # have been unprimed already
            if isinstance(trigger, GPITrigger):
                self.log.critical(f"No tasks waiting on trigger that fired: {trigger}")
                trigger.log.info("I'm the culprit")
            # For Python triggers this isn't actually an error - we might do
            # event.set() without knowing whether any tasks are actually
            # waiting on this event, for example
            elif _debug:
                self.log.debug(f"No tasks waiting on trigger that fired: {trigger}")
            return

        if _debug:
            debugstr = "\n\t".join([str(task) for task in scheduling])
            if len(scheduling) > 0:
                debugstr = "\n\t" + debugstr
            self.log.debug(
                f"{len(scheduling)} pending tasks for trigger {trigger}{debugstr}"
            )

        # queue all tasks to wake up
        for task in scheduling:
            # unset trigger
            task._trigger = None
            self._schedule_task(task)

        # cleanup trigger
        trigger._cleanup()

    def _event_loop(self) -> None:
        """Run the main event loop.

        This should only be started by:
        * The beginning of a test, when there is no trigger to react to
        * A GPI trigger
        """

        while self._scheduled_tasks and not self._terminate:
            task, outcome = self._scheduled_tasks.popitem(last=False)

            if _debug:
                self.log.debug(f"Scheduling task {task}")
            self._resume_task(task, outcome)
            if _debug:
                self.log.debug(f"Scheduled task {task}")

            # remove our reference to the objects at the end of each loop,
            # to try and avoid them being destroyed at a weird time (as
            # happened in gh-957)
            del task

            # Schedule may have queued up some events so we'll burn through those
            while self._pending_events:
                if _debug:
                    self.log.debug(
                        f"Scheduling pending event {self._pending_events[0]}"
                    )
                self._pending_events.pop(0).set()

        # no more pending tasks
        if self._terminate:
            self._handle_termination()
        elif _debug:
            self.log.debug("All tasks scheduled, handing control back to simulator")

    def _unschedule(self, task: Task[Any]) -> None:
        """Unschedule a task and unprime dangling pending triggers.

        Also:
          * enters the scheduler termination state if the Test Task is unscheduled.
          * creates and fires a :class:`~cocotb.triggers.Join` trigger.
          * forcefully ends the Test if a Task ends with an exception.
        """

        # remove task from queue
        if task in self._scheduled_tasks:
            self._scheduled_tasks.pop(task)

        # Unprime the trigger this task is waiting on
        trigger = task._trigger
        if trigger is not None:
            task._trigger = None
            if task in self._trigger2tasks.setdefault(trigger, []):
                self._trigger2tasks[trigger].remove(task)
            if not self._trigger2tasks[trigger]:
                trigger._unprime()
                del self._trigger2tasks[trigger]

        if self._terminate:
            return

        elif task.complete in self._trigger2tasks:
            self._react(task.complete)

    def _schedule_task_upon(self, task: Task[Any], trigger: Trigger) -> None:
        """Schedule `task` to be resumed when `trigger` fires."""
        # TODO Move this all into Task
        task._trigger = trigger
        task._state = Task._State.PENDING

        trigger_tasks = self._trigger2tasks.setdefault(trigger, [])
        trigger_tasks.append(task)

        if not trigger._primed:
            if trigger_tasks != [task]:
                # should never happen
                raise Exception("More than one task waiting on an unprimed trigger")

            try:
                # TODO maybe associate the react method with the trigger object so
                # we don't have to do a type check here.
                if isinstance(trigger, GPITrigger):
                    trigger._prime(self._sim_react)
                else:
                    trigger._prime(self._react)
            except Exception as e:
                # discard the trigger we associated, it will never fire
                self._trigger2tasks.pop(trigger)

                # replace it with a new trigger that throws back the exception
                self._schedule_task(task, outcome=_outcomes.Error(e))

    def _schedule_task(
        self, task: Task[Any], outcome: _outcomes.Outcome[Any] = _none_outcome
    ) -> None:
        """Queue *task* for scheduling.

        It is an error to attempt to queue a task that has already been queued.
        """
        # Don't queue the same task more than once (gh-2503)
        if task in self._scheduled_tasks:
            raise Exception("Task was queued more than once.")
        # TODO Move state tracking into Task
        task._state = Task._State.SCHEDULED
        self._scheduled_tasks[task] = outcome

    def _queue_function(self, task):
        """Queue a task for execution and move the containing thread
        so that it does not block execution of the main thread any longer.
        """
        # We should be able to find ourselves inside the _pending_threads list
        matching_threads = [
            t for t in self._pending_threads if t.thread == threading.current_thread()
        ]
        if len(matching_threads) == 0:
            raise RuntimeError("queue_function called from unrecognized thread")

        # Raises if there is more than one match. This can never happen, since
        # each entry always has a unique thread.
        (t,) = matching_threads

        async def wrapper():
            # This function runs in the scheduler thread
            try:
                _outcome = _outcomes.Value(await task)
            except (KeyboardInterrupt, SystemExit):
                # Allow these to bubble up to the execution root to fail the sim immediately.
                # This follows asyncio's behavior.
                raise
            except BaseException as e:
                _outcome = _outcomes.Error(e)
            event.outcome = _outcome
            # Notify the current (scheduler) thread that we are about to wake
            # up the background (`@external`) thread, making sure to do so
            # before the background thread gets a chance to go back to sleep by
            # calling thread_suspend.
            # We need to do this here in the scheduler thread so that no more
            # tasks run until the background thread goes back to sleep.
            t.thread_resume()
            event.set()

        event = threading.Event()
        self._schedule_task(Task(wrapper()))
        # The scheduler thread blocks in `thread_wait`, and is woken when we
        # call `thread_suspend` - so we need to make sure the task is
        # queued before that.
        t.thread_suspend()
        # This blocks the calling `@external` thread until the task finishes
        event.wait()
        return event.outcome.get()

    # This collection of functions parses a trigger out of the object
    # that was yielded by a task, converting `list` -> `Waitable`,
    # `Waitable` -> `Task`, `Task` -> `Trigger`.
    # Doing them as separate functions allows us to avoid repeating unnecessary
    # `isinstance` checks.

    def _trigger_from_started_task(self, result: Task) -> Trigger:
        if _debug:
            self.log.debug(f"Joining to already running task: {result}")
        return result.complete

    def _trigger_from_unstarted_task(self, result: Task) -> Trigger:
        self._schedule_task(result)
        if _debug:
            self.log.debug(f"Scheduling unstarted task: {result!r}")
        return result.complete

    def _trigger_from_any(self, result) -> Trigger:
        """Convert a yielded object into a Trigger instance"""
        # note: the order of these can significantly impact performance

        if isinstance(result, Trigger):
            return result

        # TODO move this into Task
        if isinstance(result, Task):
            if result._state is Task._State.UNSTARTED:
                return self._trigger_from_unstarted_task(result)
            else:
                return self._trigger_from_started_task(result)

        raise TypeError(
            f"Coroutine yielded an object of type {type(result)}, which the scheduler can't "
            f"handle: {result!r}\n"
        )

    def _resume_task(self, task: Task, outcome: _outcomes.Outcome[Any]) -> None:
        """Resume *task* with *outcome*.

        Args:
            task: The task to schedule.
            outcome: The outcome to inject into the *task*.

        Scheduling runs *task* until it either finishes or reaches the next ``await`` statement.
        If *task* completes, it is unscheduled, a Join trigger fires, and test completion is inspected.
        Otherwise, it reached an ``await`` and we have a result object which is converted to a trigger,
        that trigger is primed,
        then that trigger and the *task* are registered with the :attr:`_trigger2tasks` map.
        """
        if self._current_task is not None:
            raise Exception("_schedule() called while another Task is executing")
        try:
            self._current_task = task

            result = task._advance(outcome=outcome)

            if task.done():
                if _debug:
                    self.log.debug(f"{task} completed with {task._outcome}")
                assert result is None
                self._unschedule(task)

            # Don't handle the result if we're shutting down
            if self._terminate:
                return

            if not task.done():
                if _debug:
                    self.log.debug(f"{task!r} yielded {result} ({mycocotb.sim_phase})")
                try:
                    result = self._trigger_from_any(result)
                except TypeError as exc:
                    # restart this task with an exception object telling it that
                    # it wasn't allowed to yield that
                    self._schedule_task(task, _outcomes.Error(exc))
                else:
                    self._schedule_task_upon(task, result)

            # We do not return from here until pending threads have completed, but only
            # from the main thread, this seems like it could be problematic in cases
            # where a sim might change what this thread is.

            if self._main_thread is threading.current_thread():
                for ext in self._pending_threads:
                    ext.thread_start()
                    if _debug:
                        self.log.debug(
                            f"Blocking from {threading.current_thread()} on {ext.thread}"
                        )
                    state = ext.thread_wait()
                    if _debug:
                        self.log.debug(
                            f"Back from wait on self {threading.current_thread()} with newstate {state}"
                        )
                    if state == external_state.EXITED:
                        self._pending_threads.remove(ext)
                        self._pending_events.append(ext.event)
        finally:
            self._current_task = None

    def _cleanup(self) -> None:
        """Clear up all our state.

        Unprime all pending triggers and kill off any tasks, stop all externals.
        """
        # copy since we modify this in kill
        items = list((k, list(v)) for k, v in self._trigger2tasks.items())

        # reversing seems to fix gh-928, although the order is still somewhat
        # arbitrary.
        for _, waiting in items[::-1]:
            for task in waiting:
                if _debug:
                    self.log.debug(f"Killing {task}")
                task.kill()
            # we don't unprime trigger here since removing all tasks waiting on
            # the trigger should cause it to be unprimed in _unschedule
        assert not self._trigger2tasks

        # Kill any queued coroutines.
        # We use a while loop because task.kill() calls _unschedule(), which will remove the task from _pending_tasks.
        # If that happens a for loop will stop early and then the assert will fail.
        while self._scheduled_tasks:
            task, _ = self._scheduled_tasks.popitem(last=False)
            task.kill()

        if self._main_thread is not threading.current_thread():
            raise Exception("Cleanup() called outside of the main thread")

        for ext in self._pending_threads:
            self.log.warning(f"Waiting for {ext.thread} to exit")

    def shutdown_soon(self) -> None:
        self._terminate = True
