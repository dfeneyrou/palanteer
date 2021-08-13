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
import os
import sys
import runpy

# Local imports
from palanteer import plInitAndStart, plStopAndUninit

# This file is a part of the Palanteer Python instrumentation library
# It activates the profiling in case the package/module is "run"


if __name__ == "__main__":

    # Parse profiling options
    run_as_module = False
    record_filename, server_address, server_port = None, "127.0.0.1", 59059
    do_wait_for_server_connection, idx, do_display_usage = False, 1, False
    with_functions, with_exceptions, with_memory, with_gc, with_c_calls = (
        True,
        True,
        True,
        True,
        False,
    )
    while idx < len(sys.argv):
        if not sys.argv[idx].startswith("-"):
            break
        if sys.argv[idx] == "-nf":
            with_functions = False
        elif sys.argv[idx] == "-ne":
            with_exceptions = False
        elif sys.argv[idx] == "-nm":
            with_memory = False
        elif sys.argv[idx] == "-ng":
            with_gc = False
        elif sys.argv[idx] == "-c":
            with_c_calls = True
        elif sys.argv[idx] == "-w":
            do_wait_for_server_connection = True
        elif sys.argv[idx] == "-f" and idx + 1 < len(sys.argv):
            record_filename = sys.argv[idx + 1]
            if not record_filename.endswith(".pltraw"):
                record_filename += ".pltraw"
            idx += 1
        elif sys.argv[idx] == "-s" and idx + 1 < len(sys.argv):
            server_addr = sys.argv[idx + 1]
            idx += 1
        elif sys.argv[idx] == "-p" and idx + 1 < len(sys.argv):
            server_port = sys.argv[idx + 1]
            idx += 1
        elif sys.argv[idx] == "-m":
            run_as_module = True
        else:
            print("Error: unknown option '%s'" % sys.argv[idx], file=sys.stderr)
            do_display_usage = True
            break
        idx += 1

    # Keep only the argv related to the program to profile
    sys.argv[:] = sys.argv[idx:]

    if do_display_usage or not sys.argv:
        print(
            """Palanteer profiler usage:

Either:

 1) With code instrumentation: insert a call to palanteer.plInitAndStart(app_name, ...) in your main function.
     See Palanteer documentation for details.
     Manual instrumentation can provide much more information (data, locks, ...) than just the automatic function profiling.

 2) With unmodified script:  'python -m palanteer [options] <your script>'
     This syntax is similar to the cProfile usage. No script modification is required but only the function timings and exceptions are profiled.

Options for case (2):
 -s <server IP address> Set the server IP address (default is 127.0.0.1)
 -p <server TCP port>   Set the server port       (default is 59059)
 -f <filename.pltraw>   Save the profiling data into a file to be imported in the Palanteer viewer (default=remote connection)
 -nf                    Do not automatically log the functions              (default=log functions)
 -ne                    Do not automatically log the exceptions             (default=log exception)
 -nm                    Do not log the memory allocations                   (default=log memory allocations)
 -ng                    Do not automatically log the garbage collector runs (default=log gc)
 -c                     Do automatically log the C functions                (default=only python functions)
 -w                     Wait for server connection (Palanteer viewer or scripting module). Applicable only in case of remote connection.
 -m                     Run the app as a module (similar to python's "-m" option)

Note 1: in case of connection to the server and -w is not used and no server is reachable, profiling is simply skipped
Note 2: on both Windows and Linux, context switch information is available only with root privileges (OS limitation)
""",
            file=sys.stderr,
        )
        sys.exit(1)

    # Real work here
    if run_as_module:
        app_name = sys.argv[0]
    else:
        sys.path.insert(0, os.path.dirname(sys.argv[0]))  # Update the python path
        app_name = os.path.basename(sys.argv[0])
        if app_name.endswith(".py"):
            app_name = app_name[:-3]
    plInitAndStart(
        app_name,
        record_filename=record_filename,
        do_wait_for_server_connection=do_wait_for_server_connection,
        with_functions=with_functions,
        with_exceptions=with_exceptions,
        with_memory=with_memory,
        with_gc=with_gc,
        with_c_calls=with_c_calls,
    )
    # Execute the program
    if run_as_module:
        runpy.run_module(app_name, run_name="__main__", alter_sys=True)
    else:
        exec(
            compile(open(sys.argv[0]).read(), sys.argv[0], "exec"),
            sys._getframe(0).f_globals,
            sys._getframe(0).f_locals,
        )

    # Note: the profiling is stopped automatically at program exit
