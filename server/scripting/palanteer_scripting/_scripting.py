# Palanteer scripting library
# Copyright (C) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.


"""
This module is the Palanteer remote control Python interface.

"Look into Palanteer and get an omniscient view of your program..."

Palanteer is a multi-parts solution to software quality improvement.
This module enables remote stimulation, control and observation of an
instrumented program.
"""

import os
import sys
import re
import platform
import subprocess
import threading
import datetime
import atexit
import time
import struct

import palanteer_scripting._cextension


# Constants
# =========

_PL_FLAG_TYPE_DATA_NONE = 0
_PL_FLAG_TYPE_DATA_TIMESTAMP = 1
_PL_FLAG_TYPE_DATA_S32 = 2
_PL_FLAG_TYPE_DATA_U32 = 3
_PL_FLAG_TYPE_DATA_S64 = 4
_PL_FLAG_TYPE_DATA_U64 = 5
_PL_FLAG_TYPE_DATA_FLOAT = 6
_PL_FLAG_TYPE_DATA_DOUBLE = 7
_PL_FLAG_TYPE_DATA_STRING = 8
_PL_FLAG_TYPE_LOCK_WAIT = 16
_PL_FLAG_TYPE_LOCK_ACQUIRED = 17
_PL_FLAG_TYPE_LOCK_RELEASED = 18
_PL_FLAG_TYPE_LOCK_NOTIFIED = 19
_PL_FLAG_TYPE_MARKER = 20
_PL_FLAG_TYPE_MASK = 0x1F

# Default: "data"
_flag_to_kind = {
    _PL_FLAG_TYPE_LOCK_WAIT: "lock wait",
    _PL_FLAG_TYPE_LOCK_ACQUIRED: "lock use",
    _PL_FLAG_TYPE_LOCK_RELEASED: "lock use",
    _PL_FLAG_TYPE_LOCK_NOTIFIED: "lock notified",
    _PL_FLAG_TYPE_MARKER: "marker",
}

MATCH_EXT_STRING_LOOKUP = re.compile("@@([0-9A-F]{16})@@(.*)$")


# Exceptions
# ==========


class InitializationError(Exception):
    """The Palanteer library has not been initialized. Use the function 'initialize_scripting'."""


class ConnectionError(Exception):
    """There is no connection to the program."""


class UnknownThreadError(Exception):
    """The thread with the provided name is unknown."""


# Structures
# ==========


class Evt:
    """Represents a received event from the program under test"""

    def __init__(self, thread_name, kind, path, date_ns, value, spec_id):
        self.thread = thread_name
        self.kind = kind
        self.path = path  # List is easier to work with (checking parent, popping one level, etc...)
        self.date_ns = date_ns
        self.value = value  # Type depends on the event
        self.spec_id = spec_id
        self.children = []

    def __str__(self):
        s = [
            "%10.6f ms  kind=%-9s  thread=%s  path=%s"
            % (0.000001 * self.date_ns, self.kind, self.thread, self.path)
        ]
        if len(self.children) > 0:
            s[-1] += "  children=%d" % len(self.children)
        if len(str(self.value)):
            s[-1] += "  value=%s" % self.value
        for c in self.children:
            s.append("  | %s" % str(c))
        return "\n".join(s)


class _Elem:
    def __init__(self, nameHash, prevElemIdx, threadId, flags):
        self.nameHash = nameHash
        self.prevElemIdx = prevElemIdx
        self.threadId = threadId
        self.threadName = _event_ctx.db_thread_names[threadId]
        self.valueDecodeFormat = {
            _PL_FLAG_TYPE_DATA_S32: "i",
            _PL_FLAG_TYPE_DATA_U32: "I",
            _PL_FLAG_TYPE_DATA_S64: "q",
            _PL_FLAG_TYPE_DATA_FLOAT: "f",
            _PL_FLAG_TYPE_DATA_DOUBLE: "d",
            _PL_FLAG_TYPE_DATA_STRING: "i",
        }.get(flags & _PL_FLAG_TYPE_MASK, None)
        self.valueDecodeLength = (
            struct.calcsize(self.valueDecodeFormat) if self.valueDecodeFormat else None
        )
        eType = flags & _PL_FLAG_TYPE_MASK
        self.valueIsString = eType in (_PL_FLAG_TYPE_DATA_STRING, _PL_FLAG_TYPE_MARKER)
        self.kind = _flag_to_kind.get(eType, "data")

        # Build the path
        self.path, elemIdx = [
            _event_ctx.lkup_hash_to_string_value[nameHash]
        ], prevElemIdx
        while elemIdx >= 0:
            elem = _event_ctx.db_elems[elemIdx]
            self.path.append(_event_ctx.lkup_hash_to_string_value[elem.nameHash])
            elemIdx = elem.prevElemIdx
        self.path.reverse()


class EvtSpec:
    """Describes a subset of events to capture"""

    def __init__(self, events, thread=None, parent=None):
        self.threadName = thread if thread else ""
        self.tokenAlloc = []  # To avoid garbage collection issues...
        # Parent
        self.parentSpec = self._extractElemPath(parent)
        # Elems
        self.elemSpec = []
        if type(events) == type(""):
            events = [events]
        for e in events:
            ep = self._extractElemPath(e)
            if not ep:
                continue
            self.elemSpec.append(ep)

    def _extractElemPath(self, elemSpec):
        if elemSpec == None:
            elemSpec = ""
        result = []
        for t in [t.strip() for t in elemSpec.split("/") if t]:
            doStore = False
            if t == "*":
                doStore = True
            elif t == "**":
                doStore = (
                    result and result[-1] != "**"
                )  # Super wildcard in front is useless, same for consecutive ones
            elif t == ".":
                doStore = not result  # Must be in front to be meaningful
            else:
                doStore = True
            if doStore:
                result.append(t)

        # Beginning with "./**" voids these 2 terms
        if len(result) > 2 and result[0] == "." and result[1] == "**":
            result = result[2:]
        return tuple(result)


# Local context
# =============

# Library context

_is_initialized = False
_log_func = None
_old_excepthook = None

# Program control context


class _ProgramContext:
    def __init__(self):
        self.freeze_mode = False
        self.connection = threading.Event()
        self.lock = threading.Lock()
        self.reset()

    def reset(self, cli_to_quit=None):
        self.process = None
        self.std_out, self.std_err = None, None
        self.stdout_buffer, self.stderr_buffer = [], []
        self.cli_to_quit = cli_to_quit

    def _tee_pipe(self, is_stderr):
        # Loop on the selected readable stream (until closed)
        stdpipe = self.process.stderr if is_stderr else self.process.stdout
        for lines in stdpipe:
            # Get non empty lines
            filtered_lines = [l for l in lines.split("\n") if l.strip()]
            # Store in our buffer
            self.lock.acquire()
            if is_stderr:
                self.stderr_buffer.extend(filtered_lines)
            else:
                self.stdout_buffer.extend(filtered_lines)
            self.lock.release()


_program_ctx = _ProgramContext()

# CLI context


class _CommandContext:
    def __init__(self):
        self.lock = threading.Lock()  # Field protection
        self.answer = threading.Event()
        self.reset()

    def reset(self):
        self.answer_status = None
        self.answer_text = ""
        self.is_control_enabled = True


_command_ctx = _CommandContext()

# Evt sniffing context


class _EvtContext:
    def __init__(self):
        self.lock = threading.Lock()
        self.wake_from_events = threading.Event()
        self.specs = []  # Persistent
        self.lkup_hash_to_external_string_value = {}
        self.reset()

    def reset(self):
        self.db_thread_names = []
        self.db_elems = []
        self.db_clis = []
        self.lkup_hash_to_string_value = {}
        self.lkup_hash_to_thread_id = {}
        self.string_values = []
        self.are_strings_external = False
        self.is_short_hash = False
        self.events = []
        self.frozen_thread_bitmap = 0
        self.frozen_thread_bitmap_change = 0
        self.collection_ticks = 0


_event_ctx = _EvtContext()


# =======================
# Internal notifications
# =======================


def _notify_record_started(
    app_name, build_name, are_strings_external, is_short_hash, is_control_enabled
):
    global _program_ctx, _event_ctx, _command_ctx
    _program_ctx.lock.acquire()
    _event_ctx.are_strings_external = are_strings_external
    _event_ctx.is_short_hash = is_short_hash
    _command_ctx.is_control_enabled = is_control_enabled
    _program_ctx.connection.set()
    _program_ctx.lock.release()


def _notify_record_ended():
    global _command_ctx, _program_ctx
    _program_ctx.connection.clear()
    _command_ctx.answer.set()


def _notify_log(level, msg):
    global _log_func
    if _log_func:
        _log_func(level, msg)


def _notify_command_answer(status, answer):
    global _command_ctx
    _command_ctx.lock.acquire()
    _command_ctx.answer_status, _command_ctx.answer_text = status, answer
    _command_ctx.answer.set()
    _command_ctx.lock.release()


def _notify_new_frozen_thread_state(frozenThreadBitmap):
    global _event_ctx
    _event_ctx.lock.acquire()
    _event_ctx.frozen_thread_bitmap_change |= (
        _event_ctx.frozen_thread_bitmap ^ frozenThreadBitmap
    )  # Track the changes
    _event_ctx.frozen_thread_bitmap = frozenThreadBitmap
    _event_ctx.wake_from_events.set()
    _event_ctx.lock.release()


def _notify_new_strings(strings):
    global _event_ctx
    _event_ctx.lock.acquire()
    for h, s in strings:
        if not s and _event_ctx.are_strings_external:
            s = _event_ctx.lkup_hash_to_external_string_value.get(h, "@@%016X@@" % h)
        _event_ctx.lkup_hash_to_string_value[h] = s
        _event_ctx.string_values.append(s)
    _event_ctx.lock.release()


def _notify_new_collection_tick():
    global _event_ctx
    _event_ctx.lock.acquire()
    _event_ctx.collection_ticks += 1
    _event_ctx.wake_from_events.set()
    _event_ctx.lock.release()


def _notify_new_threads(threads):
    global _event_ctx
    _event_ctx.lock.acquire()
    for name_hash, thread_idx in threads:
        while thread_idx >= len(_event_ctx.db_thread_names):
            _event_ctx.db_thread_names.append(None)
        _event_ctx.db_thread_names[thread_idx] = _event_ctx.lkup_hash_to_string_value[
            name_hash
        ]
        _event_ctx.lkup_hash_to_thread_id[name_hash] = thread_idx
    _event_ctx.lock.release()


def _notify_new_elems(elems):
    global _event_ctx
    _event_ctx.lock.acquire()
    for name_hash, elem_idx, prev_elem_idx, thread_idx, flags in elems:
        while elem_idx >= len(_event_ctx.db_elems):
            _event_ctx.db_elems.append(None)
        while thread_idx >= len(_event_ctx.db_thread_names):
            _event_ctx.db_thread_names.append(None)
        _event_ctx.db_elems[elem_idx] = _Elem(
            name_hash, prev_elem_idx, thread_idx, flags
        )
    _event_ctx.lock.release()


def _notify_new_clis(clis):
    _event_ctx.lock.acquire()
    for name, param_spec, description in clis:
        _event_ctx.db_clis.append((name, param_spec, description))
    _event_ctx.lock.release()


def _notify_new_events(events):
    global _event_ctx
    nestedQty = 0
    _event_ctx.lock.acquire()
    for spec_id, elem_id, children_qty, name_hash, date_ns, raw_value in events:
        elem = _event_ctx.db_elems[elem_id]
        if elem.valueDecodeFormat:
            value = struct.pack("Q", raw_value)[: elem.valueDecodeLength]
            value = struct.unpack(elem.valueDecodeFormat, value)[0]
        else:
            value = raw_value
        if elem.valueIsString:
            value = _event_ctx.string_values[value]
        evt = Evt(elem.threadName, elem.kind, elem.path, date_ns, value, spec_id)
        if nestedQty == 0:
            _event_ctx.events.append(evt)
            nestedQty = children_qty
        else:
            _event_ctx.events[-1].children.append(evt)
            nestedQty = nestedQty - 1
    _event_ctx.wake_from_events.set()
    _event_ctx.lock.release()


# Public initialization API
# =========================


def _cleanup_at_exit():
    global _program_ctx
    if _program_ctx.process:
        process_stop()


def _cleanup_at_uncaught_exception(exc_type, exc_value, exc_traceback):
    _cleanup_at_exit()
    _old_excepthook(exc_type, exc_value, exc_traceback)


default_log_min_level = 2


def _default_log_func(level, msg):
    # Filtering
    if level < default_log_min_level:
        return

    date_str = datetime.datetime.today().strftime("%H:%M:%S.%f")[
        :-3
    ]  # [:-3] to remove the microseconds
    level_str = "[%s]" % {0: "detail ", 1: "info   ", 2: "warning", 3: "error  "}.get(
        level, "unknown"
    )
    print("%s %-9s %s" % (date_str, level_str, msg))


def initialize_scripting(port=59059, log_func=None):
    """Initialize the Palanteer module. It shall be called once before using any function."""
    global _is_initialized, _log_func, _old_excepthook
    if _is_initialized:
        return
    _log_func = log_func if log_func else _default_log_func

    # Initialize the extension library
    palanteer_scripting._cextension.server_start(port)

    # Register the exit function, to clean/kill all sub processes at exit
    atexit.register(_cleanup_at_exit)
    _old_excepthook = sys.excepthook
    sys.excepthook = _cleanup_at_uncaught_exception

    # Finalized
    _is_initialized = True


def uninitialize_scripting():
    """Uninitialize the Palanteer module."""
    global _is_initialized
    sys.excepthook = _old_excepthook
    _cleanup_at_exit()
    palanteer_scripting._cextension.server_stop()
    _is_initialized = False


def set_external_strings(filename=None, lkup={}):
    """
    Function to set the lookup which resolves the external strings.

    The final lookup is the contatenation of the file content and the provided 'dict'.
    """
    global _event_ctx

    _event_ctx.lkup_hash_to_external_string_value = {}
    if lkup:
        _event_ctx.lkup_hash_to_external_string_value.update(lkup)
    if filename:
        lkup = {}
        with open(filename, "r") as fHandle:
            for l in fHandle.readlines():
                m = MATCH_EXT_STRING_LOOKUP.match(l)
                if m:
                    hash_value, str_value = int(m.group(1), 16), m.group(2)
                    lkup[hash_value] = str_value
        _event_ctx.lkup_hash_to_external_string_value.update(lkup)


def hash_string(s, is_short_hash=False):
    """Fowler–Noll–Vo hash function"""
    if not is_short_hash:
        h = 14695981039346656037
        for c in s:
            h = ((h ^ ord(c)) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        if h == 0:
            h = 1  # Special case for our application (0 is reserved internally)
        return h
    else:
        h = 2166136261
        for c in s:
            h = ((h ^ ord(c)) * 16777619) & 0xFFFFFFFF
        if h == 0:
            h = 1  # Special case for our application (0 is reserved internally)
        return h


# Program control API
# ===================


def _remote_call(func, detail="", timeout_sec=5.0):
    global _command_ctx
    if not _is_initialized:
        raise InitializationError(
            "The Palanteer library has not been initialized. Use the function 'initialize_scripting'."
        )
    _command_ctx.lock.acquire()
    _command_ctx.answer.clear()
    _command_ctx.answer_status = None
    _command_ctx.answer_text = ""
    func()
    _command_ctx.lock.release()
    if not _command_ctx.answer.wait(timeout_sec):
        raise ConnectionError
    return _command_ctx.answer_status, _command_ctx.answer_text


def program_cli(command_str, timeout_sec=5.0):
    """
    Calls synchronously a remote command on the program under test.

    If there is no answer before the timeout expires, a ConnectionError exception is raised.
    The output is a tuple (status, text). A null status means success, else an error occurs and the text provides some explanation.
    """
    return _remote_call(
        lambda x=command_str: palanteer_scripting._cextension.send_cli_request(x),
        " when calling command '%s'" % command_str,
    )


def program_set_freeze_mode(state):
    """Set the 'freeze' mode on the program under test. If true, it will pause on the freeze point, else they will be ignored."""
    global _program_ctx
    _program_ctx.freeze_mode = state
    try:
        _remote_call(lambda x=state: palanteer_scripting._cextension.set_freeze_mode(x))
    except ConnectionError:
        pass  # If no connection, the state will be propagated to the process at the future creation time


def program_get_frozen_threads():
    """Returns the list of currently frozen threads."""
    global _event_ctx
    frozen_threads = []

    _event_ctx.lock.acquire()
    for h, bit in _event_ctx.lkup_hash_to_thread_id.items():
        if (1 << bit) & _event_ctx.frozen_thread_bitmap:
            frozen_threads.append(_event_ctx.lkup_hash_to_string_value[h])
    _event_ctx.lock.release()
    return frozen_threads


def program_wait_freeze(thread_names, timeout_sec=3.0):
    """Waits that all provided threads are frozen or the timeout expires."""
    global _event_ctx

    if type(thread_names) != type([]):
        thread_names = [thread_names]
    hashed_thread_names = [
        (t, hash_string(t, _event_ctx.is_short_hash)) for t in thread_names
    ]
    end_time_sec = time.time() + timeout_sec
    wait_timeout_sec = max(0.1 * timeout_sec, 0.2)

    frozen_threads = []
    while time.time() < end_time_sec:
        _event_ctx.lock.acquire()
        _event_ctx.wake_from_events.clear()
        frozen_threads = []
        for t, h in hashed_thread_names:
            bit = _event_ctx.lkup_hash_to_thread_id.get(h, None)
            if bit != None and ((1 << bit) & _event_ctx.frozen_thread_bitmap):
                frozen_threads.append(t)
        _event_ctx.lock.release()

        if len(frozen_threads) == len(thread_names):
            break  # All are frozen
        _event_ctx.wake_from_events.wait(wait_timeout_sec)

    return frozen_threads


def program_step_continue(thread_names, timeout_sec=1.0):
    """
    This function unblocks all provided threads "frozen" on a freeze point.
    Before returning, it also waits that all of them effectively change their frozen.
    It returns True if it is the case, else False after the timeout expires.
    """
    global _event_ctx

    if type(thread_names) != type([]):
        thread_names = [thread_names]
    hashed_thread_names = [
        (t, hash_string(t, _event_ctx.is_short_hash)) for t in thread_names
    ]
    end_time_sec = time.time() + timeout_sec
    wait_timeout_sec = max(0.1 * timeout_sec, 0.2)

    # Send the "step continue" command to the remote program
    _event_ctx.lock.acquire()
    thread_bitmap = 0
    for t, h in hashed_thread_names:
        bit = _event_ctx.lkup_hash_to_thread_id.get(h, None)
        if bit == None:
            raise UnknownThreadError("The thread '%s' is unknown" % t)
        thread_bitmap |= 1 << bit
    _event_ctx.frozen_thread_bitmap_change = 0  # Reset the changes
    _event_ctx.lock.release()

    _remote_call(
        lambda x=thread_bitmap: palanteer_scripting._cextension.step_continue(x)
    )

    # Wait for an effective change before leaving this function
    while time.time() < end_time_sec:
        _event_ctx.lock.acquire()
        _event_ctx.wake_from_events.clear()
        bitmap_change = _event_ctx.frozen_thread_bitmap_change
        _event_ctx.lock.release()

        if (thread_bitmap & bitmap_change) == thread_bitmap:
            return True  # All threads have changed frozen state
        _event_ctx.wake_from_events.wait(wait_timeout_sec)

    return False


def _setup_process_initialization(
    record_filename,
    pass_first_freeze_point,
    cli_to_quit,
):
    global _program_ctx, _command_ctx, _event_ctx

    # Sanity
    if not _is_initialized:
        raise InitializationError(
            "The Palanteer library has not been initialized. Use the function 'initialize_scripting'."
        )
    if _program_ctx.connection.is_set():
        raise ConnectionError(
            "Only one program at a time can be controlled and a program is already connected."
        )

    _program_ctx.reset(cli_to_quit)
    _command_ctx.reset()
    _event_ctx.reset()

    # Set the recording state
    if not record_filename:
        record_filename = ""
    if record_filename and not record_filename.endswith(".plt"):
        record_filename = record_filename + ".plt"
    palanteer_scripting._cextension.set_record_filename(record_filename)

    # Manage the synchronization freeze point
    if pass_first_freeze_point:
        program_set_freeze_mode(True)


def _connect_to_process(
    pass_first_freeze_point, connection_timeout_sec, previous_freeze_state
):
    global _program_ctx, _event_ctx

    end_synch_time_sec = time.time() + connection_timeout_sec

    # Wait the connection to Palanteer
    if not _program_ctx.connection.wait(connection_timeout_sec):
        raise ConnectionError(
            "No program connected during the timeout (%f s)." % connection_timeout_sec
        )

    # Set a small max latency, as we want script reactivity
    try:
        _remote_call(lambda: palanteer_scripting._cextension.set_max_latency_ms(10))
    except ConnectionError:
        pass

    # Release the reception thread with the first call to "set freeze mode"
    program_set_freeze_mode(_program_ctx.freeze_mode)

    # Synchronization, if required
    if pass_first_freeze_point:
        # Wait one frozen thread
        wait_timeout_sec = max(0.1 * connection_timeout_sec, 0.2)
        while time.time() < end_synch_time_sec:
            _event_ctx.lock.acquire()
            _event_ctx.wake_from_events.clear()
            frozen_thread_bitmap = _event_ctx.frozen_thread_bitmap
            _event_ctx.lock.release()
            if frozen_thread_bitmap:
                break  # At least one thread is frozen so this includes "the first one"
            _event_ctx.wake_from_events.wait(wait_timeout_sec)
        if time.time() >= end_synch_time_sec:
            raise ConnectionError(
                "Connected to the program but unable to synch on a freeze point during the timeout (%f s)."
                % connection_timeout_sec
            )

        # Put back the previous freeze state
        program_set_freeze_mode(previous_freeze_state)


def process_connect(
    record_filename="",
    pass_first_freeze_point=False,
    cli_to_quit=None,
    connection_timeout_sec=5.0,
):
    """
    This function connects to an already running process and waits the connection the Palanteer remote module.

    If no connection is established before the timeout, a ConnectionError exception is raised.
    :record_filename: name of the record file. Default is no record file.
    :cli_to_quit:     command line to call to stop the program. Default: terminate signal, then kill signal
    :connection_timeout_sec: timeout for the connection with the program
    """
    global _program_ctx

    previous_freeze_state = _program_ctx.freeze_mode
    _setup_process_initialization(
        record_filename, pass_first_freeze_point, cli_to_quit
    )

    _connect_to_process(
        pass_first_freeze_point, connection_timeout_sec, previous_freeze_state
    )


def process_launch(
    program_path,
    args=[],
    record_filename="",
    pass_first_freeze_point=False,
    capture_output=False,
    cli_to_quit=None,
    connection_timeout_sec=5.0,
):
    """
    This function launches a program and waits the connection the Palanteer remote module.

    If no connection is established before the timeout, a ConnectionError exception is raised.
    :record_filename: name of the record file. Default is no record file.
    :capture_output:  boolean. If True, the stdout and stderr are captured and accessible
                       (see 'process_get_stderr_lines' and 'process_get_stdout_lines').
    :cli_to_quit:     command line to call to stop the program. Default: terminate signal, then kill signal
    :connection_timeout_sec: timeout for the connection with the program
    """
    global _program_ctx

    previous_freeze_state = _program_ctx.freeze_mode
    _setup_process_initialization(
        record_filename, pass_first_freeze_point, cli_to_quit
    )

    # Launch the process with or without collecting the standard outputs
    if capture_output:
        _program_ctx.process = subprocess.Popen(
            [program_path] + args,
            universal_newlines=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        _program_ctx.std_out = threading.Thread(
            target=_program_ctx._tee_pipe, args=(False,)
        )
        _program_ctx.std_err = threading.Thread(
            target=_program_ctx._tee_pipe, args=(True,)
        )
        _program_ctx.std_out.start()
        _program_ctx.std_err.start()
    else:
        _program_ctx.process = subprocess.Popen(
            [program_path] + args, universal_newlines=True
        )

    _connect_to_process(
        pass_first_freeze_point, connection_timeout_sec, previous_freeze_state
    )


def process_is_running():
    """This function returns True if the launched program is still running"""
    global _program_ctx
    if not _program_ctx.process:
        return False
    _program_ctx.process.poll()  # Query the process state
    return _program_ctx.process.returncode == None


def process_get_returncode():
    """
    This function returns the exit code the launched program.

    The returns value is 'None' in case it is still running or no program has been launched."""
    global _program_ctx
    if not _program_ctx.process:
        return None
    _program_ctx.process.poll()  # Query the process state
    return _program_ctx.process.returncode


def process_get_stderr_lines():
    """
    This function returns an array with the collected text lines from the 'stderr' output.

    Applicable only if capture_output=True has been used when launching the program
    """
    global _program_ctx
    _program_ctx.lock.acquire()
    lines = _program_ctx.stderr_buffer
    _program_ctx.stderr_buffer = []
    _program_ctx.lock.release()
    return lines


def process_get_stdout_lines():
    """
    This function returns an array with the collected text lines from the 'stdout' output.

    Applicable only if capture_output=True has been used when launching the program
    """
    global _program_ctx
    _program_ctx.lock.acquire()
    lines = _program_ctx.stdout_buffer
    _program_ctx.stdout_buffer = []
    _program_ctx.lock.release()
    return lines


def process_stop():
    """This function stops the launched program"""
    global _program_ctx

    if not _program_ctx.process:
        return

    # Graceful stop: call custom command to quit, if any
    if _program_ctx.cli_to_quit:
        try:
            answer = program_cli(_program_ctx.cli_to_quit)
        except Exception:
            pass
    else:
        # A bit less graceful stop: OS terminate signal
        try:
            _program_ctx.process.terminate()
        except Exception:
            pass

    # Ensure that the process is really stopped
    try:
        _program_ctx.process.wait(timeout=0.5)
    except Exception:
        # No... we unleashed our fury now
        try:
            _program_ctx.process.kill()
            _program_ctx.process.wait(timeout=0.5)
        except Exception:
            pass

    # Process is over
    _program_ctx.process = None
    _program_ctx.connection.clear()


# Evt sniffing API
# ==================


def data_configure_events(specs):
    """
    This function configures the capture of the events. It replaces any previous configuration.

    It can be called before or while a program is running.
    It flushes all previously received events that have not been collected.
    :specs: one or a list of EvtSpec objects
    """
    global _event_ctx

    # Reset specs and buffered events
    _event_ctx.specs = []
    palanteer_scripting._cextension.clear_all_specs()
    data_clear_buffered_events()

    if type(specs) != type([]):  # Handle the single spec case
        specs = [specs]

    for specId, spec in enumerate(specs):
        palanteer_scripting._cextension.add_spec(
            spec.threadName, spec.parentSpec, spec.elemSpec
        )
        _event_ctx.specs.append(spec)


# Collect a slice of selected events until one of the exit condition is met
def data_collect_events(
    wanted=[], unwanted=[], frozen_threads=[], max_event_qty=None, timeout_sec=1.0
):
    """
    This function collects the received events from the program under control

    The selection of the events is specified with 'data_configure_events'.
    It returns an array of Evt objects if one of the condition below is fullfilled.
    :wanted: a list of event that are expected. If all of them are collected at least once, the collection is stopped.
    :unwanted: a list of event that are not expected. If one of them is collected, the collection is stopped.
    :frozen_threads: a list of thread names. If all the threads are frozen, the collection is stopped.
    :max_event_qty: integer. If the quantity of collected events reaches this value, the collection is stopped.
    :timeout_sec: float. The collection is stopped after the timeout.
    """
    global _event_ctx

    timeout_sec = max(0.01, timeout_sec)
    end_time_sec = time.time() + timeout_sec
    wait_timeout_sec = max(0.1 * timeout_sec, 0.2)  # Internal polling period
    if wanted and type(wanted) == type(""):
        wanted = [wanted]
    if unwanted and type(unwanted) == type(""):
        unwanted = [unwanted]
    if frozen_threads and type(frozen_threads) == type(""):
        frozen_threads = [frozen_threads]

    exit_loop_count, events = None, []
    while time.time() < end_time_sec:
        _event_ctx.lock.acquire()

        # Get newly received events
        new_events, _event_ctx.events = _event_ctx.events, []
        _event_ctx.wake_from_events.clear()

        # Check the exit conditions
        if exit_loop_count == None and max_event_qty and len(events) >= max_event_qty:
            exit_loop_count = (
                _event_ctx.collection_ticks
            )  # Exit without any additional tick
        if exit_loop_count == None and frozen_threads:
            thread_bitmap, areAllThreadsKnown = 0, True
            for t in frozen_threads:
                bit = _event_ctx.lkup_hash_to_thread_id.get(
                    hash_string(t, _event_ctx.is_short_hash), None
                )
                if bit != None:
                    thread_bitmap |= 1 << bit
                else:
                    areAllThreadsKnown = False
            if (
                areAllThreadsKnown
                and (_event_ctx.frozen_thread_bitmap & thread_bitmap) == thread_bitmap
            ):
                exit_loop_count = (
                    _event_ctx.collection_ticks + 2
                )  # +2 ticks for the double bank collection mechanism
        if exit_loop_count == None and wanted:
            for e in new_events:
                if e.path[-1] in wanted:
                    wanted.remove(e.path[-1])
            if not wanted:  # All wanted are found, so exit without any additional tick
                exit_loop_count = _event_ctx.collection_ticks
        if exit_loop_count == None and unwanted:
            for e in new_events:
                if e.path[-1] in unwanted:
                    exit_loop_count = (
                        _event_ctx.collection_ticks
                    )  # One unwanted is enough to exit without additional ticks
                    break
        if exit_loop_count == None and not process_is_running():
            exit_loop_count = (
                _event_ctx.collection_ticks + 2
            )  # +2 ticks for the double bank collection mechanism

        # End the iteration
        current_collection_tick = _event_ctx.collection_ticks
        _event_ctx.lock.release()
        events.extend(new_events)
        if exit_loop_count != None and exit_loop_count <= current_collection_tick:
            break
        _event_ctx.wake_from_events.wait(wait_timeout_sec)

    # Return the collected events
    return events


# Clean received but not collected yet events
def data_clear_buffered_events():
    """This function clears all buffered events that have not been collected"""
    global _event_ctx
    _event_ctx.lock.acquire()
    _event_ctx.events = []
    palanteer_scripting._cextension.clear_buffered_events()
    _event_ctx.lock.release()


# Output is a list of (spec id, event spec, unresolved explanation message)
def data_get_unresolved_events():
    """
    This function returns a list of unresolved specified events.

    The tuples in this list are: (spec index, event specification, unresolved explanation message)
    """
    global _event_ctx

    # Get the infos
    ueiList = palanteer_scripting._cextension.get_unresolved_elem_infos()

    # Format the output
    outputInfos = []
    for spec_id, elem_id, error_msg in ueiList:
        f = _event_ctx.specs[spec_id]
        outputInfos.append(
            (spec_id, f.threadName, f.parentSpec, f.elemSpec[elem_id], error_msg)
        )
    return outputInfos


# Output is a list of thread names
def data_get_known_threads():
    """This function returns a list containing the names of the known threads."""
    global _event_ctx
    _event_ctx.lock.acquire()
    known_threads = [
        _event_ctx.lkup_hash_to_string_value[h]
        for h in _event_ctx.lkup_hash_to_thread_id.keys()
    ]
    _event_ctx.lock.release()
    return known_threads


# Output is a list of (path, kind, thread name)
def data_get_known_event_kinds():
    """
    This function returns a list containing the known event kinds.

    The tuples in this list are: (path of the event kind, kind of event, name of the thread)
    """
    global _event_ctx
    _event_ctx.lock.acquire()
    ec = _event_ctx
    known_event_kinds = [
        (e.path, e.kind, ec.db_thread_names[e.threadId])
        for e in ec.db_elems
        if e != None
    ]
    _event_ctx.lock.release()
    return known_event_kinds


# Output is the list of known CLI
def data_get_known_clis():
    """
    This function returns a list containing the known CLIs.

    The tuples in this list are: (CLI name, parameter specification, description)
    """
    global _event_ctx
    _event_ctx.lock.acquire()
    ec = _event_ctx
    known_clis = _event_ctx.db_clis[:]
    _event_ctx.lock.release()
    return known_clis


# Debug API
# =========


def debug_print_unresolved_events(output_file=sys.stdout):
    """This function displays the list of the unresolved specified events"""

    unresolved_event = data_get_unresolved_events()
    print("Unresolved events (%d):" % len(unresolved_event), file=output_file)
    for spec_id, thread_name, parent_spec, event_spec, msg in unresolved_event:
        print(
            "  - From spec #%d, %s for event '%s'%s%s"
            % (
                spec_id,
                msg,
                "/".join(event_spec),
                (" with parent '%s'" % "/".join(parent_spec)) if parent_spec else "",
                (" with thread '%s'" % thread_name) if thread_name else "",
            ),
            file=output_file,
        )


def debug_print_known_threads(output_file=sys.stdout):
    """This function displays the list of names of the known threads"""

    thread_names = data_get_known_threads()
    print("Known threads (%d):" % len(thread_names), file=output_file)
    for t in thread_names:
        print("  - %s" % t, file=output_file)


def debug_print_known_event_kinds(output_file=sys.stdout):
    """This function displays the list of the known event kinds"""

    event_kinds = data_get_known_event_kinds()
    print("Known event kinds (%d):" % len(event_kinds), file=output_file)
    event_kinds.sort(key=lambda x: (x[2].lower(), x[1].lower(), x[0]))
    for path, kind, thread_name in event_kinds:
        print(
            "  - %-11s %-24s : %s" % ("[%s]" % kind, thread_name, "/".join(path)),
            file=output_file,
        )


def debug_print_known_clis(output_file=sys.stdout):
    """This function displays the list of the known CLIs"""

    clis = data_get_known_clis()
    print("Known CLIs (%d):" % len(clis), file=output_file)
    clis.sort(key=lambda x: x[0].lower())
    for name, param_spec, description in clis:
        print(
            "  - %s  %s\n      %s" % (name, param_spec, description), file=output_file
        )
