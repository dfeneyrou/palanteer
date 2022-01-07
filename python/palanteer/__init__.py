# The MIT License (MIT)
#
# Copyright(c) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files(the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions :
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# System imports
import sys
import gc
import threading

# Sanity check
if sys.version_info < (3, 6, 0):
    raise Exception("Palanteer Python library requires at least Python 3.6")

# Local import, which "turns" the package into a module and mixes both Python (this file) and C-extension API
from palanteer._cextension import *
import palanteer._cextension


# Package state
_old_excepthook = None
_is_activated = False
_is_with_functions = True

# Log levels
(
    LOG_LEVEL_ALL,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE,
) = (
    0,
    0,
    1,
    2,
    3,
    4,
)


# Deprecated function
from warnings import warn


def plMarker(category, msg):
    warn(
        "plMarker is deprecated and will be removed soon. Use plLogDebug, plLogInfo, plLogWarn or plLogError instead.",
        DeprecationWarning,
        stacklevel=2,
    )
    plLogWarn(category, msg)


# Decorator to identify test functions
def plFunction(func):
    global _is_activated, _is_with_functions
    # Do nothing if automatic function logging is enabled
    if _is_activated and _is_with_functions:
        return func
    # Define the instrumented function (this function call is masked)
    def _plFunctionInner(*args, **kwargs):
        global _is_activated, _is_with_functions
        if _is_activated and _is_with_functions:
            return func(*args, **kwargs)
        plBegin(func.__name__)
        returned_value = func(*args, **kwargs)
        plEnd(func.__name__)
        return returned_value

    # Return our instrumented function
    return _plFunctionInner


# Collect garbage collection information  (this function call is masked)
def _pl_garbage_collector_notif(phase, info):
    # Log as a scope and as a "lock", which provides a global view of the GC usage
    if phase == "start":
        plBegin("Garbage collection")
        palanteer._cextension.plLockState("Garbage collection", True)
    else:
        palanteer._cextension.plLockState("Garbage collection", False)
        plEnd("Garbage collection")


def plInitAndStart(
    app_name,
    record_filename=None,
    build_name=None,
    server_address="127.0.0.1",
    server_port=59059,
    do_wait_for_server_connection=False,
    with_functions=True,
    with_exceptions=True,
    with_memory=True,
    with_gc=True,
    with_c_calls=False,
):
    global _old_excepthook, _is_activated, _is_with_functions
    if _is_activated:
        return
    _is_activated = True
    _is_with_functions = with_functions

    # This Python callback is required by the threading.setprofile()  (no C equivalent for multi-threading)
    # The first event arrives here, and is re-routed on the C bootstrap which replaces it with the C callback.
    # All subsequent events are then processed by the C callback.
    def bootstrap_callback(frame, event, arg):
        palanteer._cextension._profiling_bootstrap_callback(frame, event, arg)

    # Ensure that (not too) brutal ending is covered
    import atexit

    def notify_uncaught_exception(exc_type, exc_value, exc_traceback):
        global _old_excepthook
        palanteer._cextension.plLogError(
            "Exception", "Last exception was uncaught, exiting program"
        )
        _old_excepthook(exc_type, exc_value, exc_traceback)

    atexit.register(plStopAndUninit)
    _old_excepthook = sys.excepthook
    sys.excepthook = notify_uncaught_exception

    # Collect garbage collection information
    if with_gc:
        gc.callbacks.append(_pl_garbage_collector_notif)

    # Start the instrumentation
    if with_functions or with_exceptions:
        threading.setprofile(
            bootstrap_callback
        )  # Hook our callback to detect all new threads
    palanteer._cextension._profiling_start(
        app_name,
        record_filename,
        build_name,
        server_address,
        server_port,
        1 if do_wait_for_server_connection else 0,
        1 if with_functions else 0,
        1 if with_exceptions else 0,
        1 if with_memory else 0,
        1 if with_c_calls else 0,
    )
    # Note: the first automatic "leave function" event will be dropped (which correspond to leaving this function)


def plStopAndUninit():
    global _mode, _is_activated
    if not _is_activated:
        return
    _is_activated = False

    # Get back all possible memory
    gc.collect()

    # Stop the instrumentation
    palanteer._cextension._profiling_stop()

    # De-register from the garbage collection callbacks
    if _pl_garbage_collector_notif in gc.callbacks:
        gc.callbacks.remove(_pl_garbage_collector_notif)
