#! /usr/bin/env python3

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

USAGE = r"""This "yet another test framework" is a small and functional show-case, without external dependencies.
It has two purposes:
  1) show how to build a script based on Palanteer and how to integrate it in a real test framework
  2) test the Palanteer project for real

Tests are grouped per "suite". One suite is the minimum granularity you can launch.
Tests contain checkpoints which are binary results with a description. First failed checkpoint
stops the test and make it fail.
Some Key Performance Indicators, or performance measures, can also be collected while testing.

A test is a Python function identified by a decorator:
 * @declare_test("suite name")
The test execution order is per suite, then per filename (in case a suite is located in different files),
then per location inside files.

A suite can be "prepared" and "cleaned" with functions having resectively the following decorators:
 * @prepare_suite("suite name")
 * @clean_suite  ("suite name")
Some global preparation and cleaning can be done by using an empty suite name.

A test shall use the 3 following primitives from this test framework:
 - LOG(description)
   - log an informative message which explains what you plan to do
 - CHECK(boolean status, description [, variable to display when failed])
   - declare a checkpoint
 - KPI(name, value)
   - save a measure named "name" with the value "value"

Output is on stdout and the final result is reflected in the tool execution status.

Syntax: %s [options] <test folder>
Options are:
   -v   : verbose mode, details of tests are displayed. Default: details are displayed only for failed tests
   -f   : exit at first test failed
   -l   : list the found tests and exits. Default is test execution
   -nc  : no escape color codes
   -s  <suite>  : filter in the provided suite. May be used several times
   -ns <suite>  : filter out the provided suite. May be used several times
"""

# Imports
import sys
import os
import time
import glob
import inspect
import builtins

try:
    import palanteer_scripting
except ModuleNotFoundError:
    print(
        "ERROR: Palanteer scripting module is not installed, unable to run the test framework",
        file=sys.stderr,
    )
    sys.exit(1)

# Decorator to identify test functions
def declare_test(suite=""):
    def inner_with_param(func):
        def test_func(*args, **kwargs):
            returned_value = func(*args, **kwargs)
            return returned_value

        test_func.suite, test_func.__doc__, test_func.module = (
            suite,
            func.__doc__,
            func.__module__,
        )
        if not test_func.__doc__:
            test_func.__doc__ = "**No test description**"
        test_func.sort_key = (
            inspect.getsourcefile(func),
            inspect.getsourcelines(func)[1],
        )  # Set to the tuple (file, line number) to keep the order from source
        return test_func

    return inner_with_param


# Decorator to identify "prepare" functions
def prepare_suite(suite=""):
    def inner_with_param(func):
        def prepare_func(*args, **kwargs):
            returned_value = func(*args, **kwargs)
            return returned_value

        prepare_func.suite = suite
        return prepare_func

    return inner_with_param


# Decorator to identify "clean" functions
def clean_suite(suite=""):
    def inner_with_param(func):
        def clean_func(*args, **kwargs):
            returned_value = func(*args, **kwargs)
            return returned_value

        clean_func.suite = suite
        return clean_func

    return inner_with_param


# Log function, just to make the test plan more understandable
# msg: a description
def LOG(msg):
    text = "%6.3f)   %s" % (time.time() - builtins.test_ctx.time_origin, msg)
    if builtins.test_ctx.is_verbose:
        print(text)
    else:
        builtins.test_ctx.current_test_log.append(text)


# Check function which registers a boolean check result
# status: boolean for success state
# msg   : description of the check
# args  : additional values to dump in case of failure
def CHECK(status, msg, *args):
    builtins.test_ctx.current_test_check_qty += 1
    caller = inspect.getframeinfo(inspect.stack()[1][0])
    if status:
        text = "%6.3f)     %s[OK]%s %s%s" % (
            time.time() - builtins.test_ctx.time_origin,
            builtins.test_ctx.GREEN,
            builtins.test_ctx.DWHITE,
            msg,
            builtins.test_ctx.NORMAL,
        )
        if builtins.test_ctx.is_verbose:
            print(text)
        else:
            builtins.test_ctx.current_test_log.append(text)
    else:
        text = [
            "%6.3f)     %sFAILED%s checking '%s' at [%s:%d]"
            % (
                time.time() - builtins.test_ctx.time_origin,
                builtins.test_ctx.RED,
                builtins.test_ctx.NORMAL,
                msg,
                caller.filename,
                caller.lineno,
            )
        ]
        for detail in args:
            text.append(
                "%s%s%s"
                % (builtins.test_ctx.DWHITE, str(detail), builtins.test_ctx.NORMAL)
            )
        if builtins.test_ctx.is_verbose:
            print("\n".join(text))
        else:
            builtins.test_ctx.current_test_log.extend(text)
        raise builtins.TestFailedError()


# Log a Key Performance Indicator, which will be summarized at the end of the campaign
# name : name of the KPI
# value: value of the KPI
def KPI(name, value):
    LOG("New Key Performance Indicator: '%s'=%s" % (name, value))
    builtins.test_ctx.kpi[name] = value


# Internal test framework context
class _Context:
    def __init__(self):
        (
            self.RED,
            self.GREEN,
            self.CYAN,
            self.YELLOW,
            self.PURPLE,
            self.DWHITE,
            self.NORMAL,
        ) = (
            "\033[91m",
            "\033[92m",
            "\033[96m",
            "\033[93m",
            "\033[95m",
            "\033[37m",
            "\033[0m",
        )
        self.current_test_log = []
        self.current_test_check_qty = 0
        self.kpi = {}
        self.is_verbose = False
        self.time_origin = time.time()


# Internal test failure exception
class _TestFailedError(Exception):
    """Failed checkpoint"""


# Public CRASH_SPEC
CRASH_SPEC = [
    palanteer_scripting.EvtSpec("CRASH Stacktrace"),
    palanteer_scripting.EvtSpec(parent="CRASH Stacktrace", events=["*"]),
]

# Used server port (better if different from the viewer's one)
SERVER_PORT = 59060

# Main entry
# ==========


def main(argv):

    # Set the global context
    # The "global variable module" pattern does not fit because we want:
    #  - a one file test framework (simple for user)
    #  - be able to "import * from testframework" in test files, so that APIs are easy to call (as they are upper case, it is hard to miss them)
    builtins.test_ctx = _Context()
    builtins.TestFailedError = _TestFailedError

    # Command line parameters parsing
    # ===============================
    folder = None
    noColor = False
    doPrintUsage = False
    doList = False
    doStopAtFirstFail = False
    infilter_suite = []
    outfilter_suite = []
    i = 1
    while i < len(argv):

        if argv[i].lower() in ["-l", "/l"]:
            doList = True
        elif argv[i].lower() in ["-nc", "/nc"]:
            noColor = True
        elif argv[i].lower() in ["-v", "/v"]:
            builtins.test_ctx.is_verbose = True
        elif argv[i].lower() in ["-f", "/f"]:
            doStopAtFirstFail = True
        elif argv[i].lower() in ["-s", "/s"] and i + 1 < len(argv):
            i = i + 1
            infilter_suite.append(argv[i].lower())
        elif argv[i].lower() in ["-ns", "/ns"] and i + 1 < len(argv):
            i = i + 1
            outfilter_suite.append(argv[i].lower())
        elif argv[i][0] == "-":
            doPrintUsage = True  # Unknown option
            print("Unknown option '%s'" % argv[i], file=sys.stderr)
        else:
            if folder:
                doPrintUsage = True  # Only one folder parameter
            folder = argv[i]
        i = i + 1

    if noColor:
        (
            builtins.test_ctx.RED,
            builtins.test_ctx.GREEN,
            builtins.test_ctx.CYAN,
            builtins.test_ctx.YELLOW,
            builtins.test_ctx.PURPLE,
            builtins.test_ctx.DWHITE,
            builtins.test_ctx.NORMAL,
        ) = ("", "", "", "", "", "", "")
    if not folder:
        doPrintUsage = True
    if doPrintUsage:
        print(USAGE % argv[0], file=sys.stderr)
        sys.exit(1)

    # Inner logging function
    def format_dynamic_library_logs(level, msg):
        if level >= 2:  # Below this level, logs not that important
            level_str, level_color = {
                0: ("detail", builtins.test_ctx.DWHITE),
                1: ("info", builtins.test_ctx.NORMAL),
                2: ("warning", builtins.test_ctx.YELLOW),
                3: ("error", builtins.test_ctx.YELLOW),
            }.get(level, "unknown")
            text = "%6.3f)       %s[%s]%s %s" % (
                time.time() - builtins.test_ctx.time_origin,
                level_color,
                level_str,
                builtins.test_ctx.NORMAL,
                msg,
            )
            if builtins.test_ctx.is_verbose:
                print(text)
            else:
                builtins.test_ctx.current_test_log.append(text)

    palanteer_scripting.initialize_scripting(
        port=SERVER_PORT, log_func=format_dynamic_library_logs
    )

    # Collect the tests from all python files in the provided folder
    # ==============================================================
    # Add the folder at the start of the PYTHON PATH so that we can import the files.
    # We suppose here that the present files will not hide a useful standard module...
    sys.path.insert(0, folder)

    test_func_list, prepare_func_per_suite, clean_func_per_suite = [], {}, {}
    for f in glob.glob("%s/*.py" % folder):
        # Load the module
        moduleName = os.path.basename(f)[:-3]
        testModule = __import__(moduleName)

        # Look for decorated functions
        for name, value in inspect.getmembers(testModule):
            if not inspect.isfunction(value) or value.__module__ != "testframework":
                continue
            if value.__name__ == "test_func":  # Test function?
                value.idx = (
                    inspect.getsourcefile(value),
                    inspect.getsourcelines(value)[1],
                )
                test_func_list.append(value)
            elif value.__name__ == "prepare_func":  # Suite preparation function?
                prepare_func_per_suite[value.suite] = value
            elif value.__name__ == "clean_func":  # Suite cleaning function?
                clean_func_per_suite[value.suite] = value

    # Filter and sort by test suites
    if infilter_suite:
        test_func_list = [
            f for f in test_func_list if f.suite.lower() in infilter_suite
        ]
    if outfilter_suite:
        test_func_list = [
            f for f in test_func_list if f.suite.lower() not in outfilter_suite
        ]
    test_func_list.sort(key=lambda x: (x.suite, x.sort_key))
    test_descr_max_len = max([0] + [len(f.__doc__) for f in test_func_list])
    suite_max_len = max([0] + [len(f.suite) for f in test_func_list])

    if doList:
        print("List of tests (%d):\n===================" % len(test_func_list))
        for t in test_func_list:
            print(
                "%s%s%s%s:   %s%s%s"
                % (
                    builtins.test_ctx.PURPLE,
                    t.suite,
                    builtins.test_ctx.NORMAL,
                    " " * (suite_max_len - len(t.suite)),
                    builtins.test_ctx.CYAN,
                    t.__doc__,
                    builtins.test_ctx.NORMAL,
                )
            )
        sys.exit(0)

    # Run the tests
    # =============
    last_test, add_spacing_before_suite, add_spacing_before_test, results = (
        None,
        True,
        True,
        [],
    )

    # Global preparation
    if "" in prepare_func_per_suite:
        prepare_func_per_suite[""]()

    # Loop on test to pass
    for t in test_func_list:

        # Display test suite title
        if last_test == None or t.suite != last_test.suite:

            # Clean the tests for the previous suite
            if last_test and last_test.suite in clean_func_per_suite:
                clean_func_per_suite[last_test.suite]()

            print(
                "%s%sTest suite: %s\n%s%s"
                % (
                    "" if add_spacing_before_suite else "\n",
                    builtins.test_ctx.PURPLE,
                    t.suite,
                    "=" * (12 + len(t.suite)),
                    builtins.test_ctx.NORMAL,
                )
            )
            add_spacing_before_suite, add_spacing_before_test, last_test = (
                False,
                True,
                t,
            )

            # Prepare the tests for this suite
            if t.suite in prepare_func_per_suite:
                prepare_func_per_suite[t.suite]()

        # Execute the test
        (
            builtins.test_ctx.current_test_check_qty,
            builtins.test_ctx.current_test_log,
            success,
        ) = (0, [], True)
        try:
            print(
                "%s%sTest '%s'%s%s  "
                % (
                    "" if add_spacing_before_test else "\n",
                    builtins.test_ctx.CYAN,
                    t.__doc__,
                    builtins.test_ctx.NORMAL,
                    " " * (test_descr_max_len - len(t.__doc__)),
                ),
                flush=True,
                end="\n" if builtins.test_ctx.is_verbose else "",
            )
            add_spacing_before_test = not builtins.test_ctx.is_verbose
            start_time_sec = time.time()

            # Run, test, run!
            t()
        except builtins.TestFailedError:
            success = False  # Tests are stopped at first failure (to limit the cascading effect)
            if not builtins.test_ctx.is_verbose:
                print(
                    "\n" + ("\n".join(builtins.test_ctx.current_test_log))
                )  # Failed test is always verbose
        except Exception as e:
            if not builtins.test_ctx.is_verbose:
                print("\n" + ("\n".join(builtins.test_ctx.current_test_log)))
            raise

        # Update the results
        duration_sec = time.time() - start_time_sec
        if not results or results[-1][0] != t.suite:
            results.append(
                [t.suite, 0, 0, 0.0]
            )  # [ suite name, test successful, test count, time spent ]
        results[-1][1] += 1 if success else 0  # successful tests
        results[-1][2] += 1  # test count
        results[-1][3] += duration_sec  # time spent
        if builtins.test_ctx.is_verbose:
            print(
                "   %s=> %s %s%s"
                % (
                    builtins.test_ctx.GREEN if success else builtins.test_ctx.RED,
                    t.__doc__,
                    "OK" if success else "FAILED",
                    builtins.test_ctx.NORMAL,
                )
            )
        else:
            print(
                "%s[%s] %s(%d check%s in %.1f s)%s"
                % (
                    builtins.test_ctx.GREEN if success else builtins.test_ctx.RED,
                    "OK" if success else "FAILED",
                    builtins.test_ctx.DWHITE,
                    builtins.test_ctx.current_test_check_qty,
                    "s" if builtins.test_ctx.current_test_check_qty > 1 else "",
                    duration_sec,
                    builtins.test_ctx.NORMAL,
                )
            )
        if doStopAtFirstFail and not success:
            break

    # Clean the last suite
    if last_test and last_test.suite in clean_func_per_suite:
        clean_func_per_suite[last_test.suite]()

    # Global cleaning
    if "" in clean_func_per_suite:
        clean_func_per_suite[""]()

    # Display the synthetic results, clean and exit
    # =============================================
    kpi_max_len = max([0] + [len(k) for k in builtins.test_ctx.kpi])
    name_max_len = max(suite_max_len, kpi_max_len)
    title_bar_len = name_max_len + 31 + 2
    print(
        "\nFinal results (%d tests in %.1f s):\n%s"
        % (
            sum([r[2] for r in results]),
            sum([r[3] for r in results]),
            "=" * title_bar_len,
        )
    )
    global_success = True
    for r in results:
        global_success = global_success and r[1] == r[2]
        print(
            "%s%s%s%s | %s%2d/%2d tests passed%s | %5.1f s |"
            % (
                builtins.test_ctx.PURPLE,
                r[0],
                builtins.test_ctx.NORMAL,
                " " * (name_max_len - len(r[0])),
                builtins.test_ctx.GREEN if r[1] == r[2] else builtins.test_ctx.RED,
                r[1],
                r[2],
                builtins.test_ctx.NORMAL,
                r[3],
            )
        )
    print("=" * title_bar_len)
    if builtins.test_ctx.kpi:
        for k in sorted(builtins.test_ctx.kpi.keys()):
            print(
                "%s%s%s%s | %-28s |"
                % (
                    builtins.test_ctx.YELLOW,
                    k,
                    builtins.test_ctx.NORMAL,
                    " " * (name_max_len - len(k)),
                    builtins.test_ctx.kpi[k],
                )
            )
        print("=" * title_bar_len)

    palanteer_scripting.uninitialize_scripting()
    sys.exit(0 if global_success else 1)


# Bootstrap
# =========
if __name__ == "__main__":
    main(sys.argv)
