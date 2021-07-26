#! /usr/bin/env python3

# System import
import sys
import time

# Local import
from testframework import *  # Decorators, LOG, CHECK, KPI
from test_base import *  # Test common helpers
from palanteer_scripting import *  # Palanteer API

# These tests does some performance measurement

# C++ program to evaluate performances
PERF_CODE = r"""#include <cstdlib>
#define PL_IMPLEMENTATION 1
#define PL_IMPL_COLLECTION_BUFFER_BYTE_QTY 60000000
#include "palanteer.h"

int main(int argc, char** argv)
{
    plInitAndStart("measure_event_size_and_timing", PL_MODE_STORE_IN_FILE);
    plDeclareThread("Main");
    volatile int abcdefghij = atoi(argv[1]);
#define EVT1()   %s
#define EVT8()   EVT1() EVT1() EVT1() EVT1() EVT1() EVT1() EVT1() EVT1()
#define EVT64()  EVT8() EVT8() EVT8() EVT8() EVT8() EVT8() EVT8() EVT8()
    for(int i=0; i<10000; ++i) { // 1024 instances
         EVT64() EVT64() EVT64() EVT64() EVT64() EVT64() EVT64() EVT64()
         EVT64() EVT64() EVT64() EVT64() EVT64() EVT64() EVT64() EVT64()
    }
    plStopAndUninit();
    return 0;
}
"""

# C++ program to evaluate the cost of the header instrumentation library
HEADER_CODE = r"""#include <cstdlib>
%s

int main(int argc, char** argv) {
    return 0;
}
"""


def _evaluate_perf_program(eval_content, flags, loop=3, code=PERF_CODE):
    LOG(
        "Experiment with evaluation '%s' and flags '%s'"
        % (eval_content, " ".join(flags))
    )

    # Create the source file
    fh = open("test_performance.cpp", "w")
    fh.write(code % eval_content)
    fh.close()

    # Measure the build time
    build_time_sec = 1000.0
    for i in range(loop):
        start_sec = time.time()
        if sys.platform == "win32":
            run_cmd(
                ["cl.exe", "test_performance.cpp", "-I", "..\\..", "/EHs", "/Fea.exe"]
                + flags
            )
        else:
            run_cmd(["g++", "test_performance.cpp", "-I", "../..", "-lpthread"] + flags)
        build_time_sec = min(build_time_sec, time.time() - start_sec)
    if sys.platform == "win32":
        prog_name = "a.exe"
    else:
        prog_name = "a.out"
        run_cmd(["strip", prog_name])

    # Measure the program size
    program_size = os.stat(prog_name).st_size
    # Measure the execution time
    exec_time_sec = 1000.0
    for i in range(loop):
        start_sec = time.time()
        run_cmd([prog_name, "14"])
        exec_time_sec = min(exec_time_sec, time.time() - start_sec)

    # Return the performances
    LOG(
        "    build time=%.2f, program size=%d bytes, exec time=%.2f s"
        % (build_time_sec, program_size, exec_time_sec)
    )
    return build_time_sec, program_size, exec_time_sec


@declare_test("performance")
def measure_event_size_and_timing():
    """Measure code size, build time and execution time of some specific parts of Palanteer"""

    # Cost of including the header
    inc0_built_time_sec, inc0_program_size, inc0_exec_time_sec = _evaluate_perf_program(
        "", [], code=HEADER_CODE
    )
    inc1_built_time_sec, inc1_program_size, inc1_exec_time_sec = _evaluate_perf_program(
        '#include "palanteer.h"', [], code=HEADER_CODE
    )
    inc2_built_time_sec, inc2_program_size, inc2_exec_time_sec = _evaluate_perf_program(
        '#include "palanteer.h"', ["-DUSE_PL=1"], code=HEADER_CODE
    )
    inc3_built_time_sec, inc3_program_size, inc3_exec_time_sec = _evaluate_perf_program(
        '#define PL_IMPLEMENTATION 1\n#include "palanteer.h"',
        ["-DUSE_PL=1"],
        code=HEADER_CODE,
    )
    inc4_built_time_sec, inc4_program_size, inc4_exec_time_sec = _evaluate_perf_program(
        '#include "palanteer.h"\n#include <thread>', [], code=HEADER_CODE
    )

    # Fully disabled Palanteer
    (
        noplO_build_time_sec,
        noplO_program_size,
        noplO_exec_time_sec,
    ) = _evaluate_perf_program("plVar(abcdefghij);", ["-DUSE_PL=0", "-O2"])

    # Optimized event measures
    refO_build_time_sec, refO_program_size, refO_exec_time_sec = _evaluate_perf_program(
        "", ["-DUSE_PL=1", "-O2"]
    )
    (
        ref1O_build_time_sec,
        ref1O_program_size,
        ref1O_exec_time_sec,
    ) = _evaluate_perf_program("", ["-DUSE_PL=1", "-DPL_IMPL_CONTEXT_SWITCH=0", "-O2"])
    (
        ref2O_build_time_sec,
        ref2O_program_size,
        ref2O_exec_time_sec,
    ) = _evaluate_perf_program("", ["-DUSE_PL=1", "-DPL_NOCONTROL=1", "-O2"])
    evtO_build_time_sec, evtO_program_size, evtO_exec_time_sec = _evaluate_perf_program(
        "plVar(abcdefghij);", ["-DUSE_PL=1", "-O2"]
    )

    # Optimized assert measures
    (
        assertO_build_time_sec,
        assertO_program_size,
        assertO_exec_time_sec,
    ) = _evaluate_perf_program("plAssert(abcdefghij++);", ["-DUSE_PL=1", "-O2"], 1)
    (
        assertEO_build_time_sec,
        assertEO_program_size,
        assertEO_exec_time_sec,
    ) = _evaluate_perf_program(
        "plAssert(abcdefghij++, abcdefghij, i);", ["-DUSE_PL=1", "-O2"], 1
    )
    (
        refSO_build_time_sec,
        refSO_program_size,
        refSO_exec_time_sec,
    ) = _evaluate_perf_program("", ["-DUSE_PL=1", "-DPL_SIMPLE_ASSERT=1", "-O2"], 1)
    (
        assertSO_build_time_sec,
        assertSO_program_size,
        assertSO_exec_time_sec,
    ) = _evaluate_perf_program(
        "plAssert(abcdefghij++, abcdefghij, i);",
        ["-DUSE_PL=1", "-DPL_SIMPLE_ASSERT=1", "-O2"],
        1,
    )

    # Debug event measures (for build timings only)
    debugOpt = "-Od" if sys.platform == "win32" else "-O0"
    refg_build_time_sec, refg_program_size, refg_exec_time_sec = _evaluate_perf_program(
        "", ["-DUSE_PL=1", debugOpt]
    )
    (
        evtg_build_time_sec,
        small_program_size,
        small_exec_time_sec,
    ) = _evaluate_perf_program("plVar(abcdefghij);", ["-DUSE_PL=1", debugOpt])

    # Log the KPIs
    KPI(
        "Compilation speed (-O2)",
        "%3d event/s" % (1024.0 / (evtO_build_time_sec - refO_build_time_sec)),
    )
    KPI(
        "Compilation speed (%s)" % debugOpt,
        "%3d event/s" % (1024.0 / (evtg_build_time_sec - refg_build_time_sec)),
    )
    KPI(
        "Event code size (-O2)",
        "%d bytes/event" % int((evtO_program_size - refO_program_size) / 1024),
    )
    KPI(
        "Event logging runtime",
        "%.1f ns"
        % (
            1000000000.0 / (1024 * 100000.0) * (evtO_exec_time_sec - refO_exec_time_sec)
        ),
    )

    KPI(
        "Assert code size",
        "%.1f bytes/assert" % ((assertO_program_size - refO_program_size) / 1024.0),
    )
    KPI(
        "Assert+2 integers code size",
        "%.1f bytes/assert" % ((assertEO_program_size - refO_program_size) / 1024.0),
    )
    KPI(
        "Assert (simple) code size",
        "%.1f bytes/assert" % ((assertSO_program_size - refSO_program_size) / 1024.0),
    )

    KPI(
        "Palanteer code size - Total (-O2)",
        "%d bytes" % int(refO_program_size - noplO_program_size),
    )
    KPI(
        "Palanteer code size - Context switch (-O2)",
        "%d bytes" % int(refO_program_size - ref1O_program_size),
    )
    KPI(
        "Palanteer code size - Control part (-O2)",
        "%d bytes" % int(refO_program_size - ref2O_program_size),
    )

    KPI(
        "Palanteer include - USE_PL=0",
        "%.3f s" % (inc1_built_time_sec - inc0_built_time_sec),
    )
    KPI(
        "Palanteer include - USE_PL=1",
        "%.3f s" % (inc2_built_time_sec - inc0_built_time_sec),
    )
    KPI(
        "Palanteer include - USE_PL=1 + impl.",
        "%.3f s" % (inc3_built_time_sec - inc0_built_time_sec),
    )
    KPI(
        "Palanteer include - USE_PL=0 + <thread>",
        "%.3f s" % (inc4_built_time_sec - inc0_built_time_sec),
    )
