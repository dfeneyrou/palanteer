
## Look into Palanteer and get an omniscient view of your program

Palanteer is a set of lean and efficient tools to improve the quality of software for Python programs (and C++).

Simple code instrumentation, mostly automatic in Python, delivers powerful features:
  - **Collection of meaningful atomic events** on timings, memory, locks wait and usage, context switches, data values..
  - **Visual and interactive observation** of record: timeline, plot, histograms, flame graph...
  - **Remote command call and events observation can be scripted in Python**: deep testing has never been simpler

Execution of unmodified Python programs can be analyzed directly with a syntax similar to the one of `cProfile`:
   - Functions enter/leave
   - Interpreter memory allocations
   - All raised exceptions
   - Garbage collection runs
   - Support of multithread, coroutines, asyncio/gevent

The collected events can either be **processed automatically by a script**, or analyzed with the **separate viewer** (see last section):
<img src="https://dfeneyrou.github.io/palanteer/images/views.gif" alt="Palanteer viewer image" width="1000"/>

Palanteer is an efficient, lean and comprehensive solution for better and enjoyable software development!

## Usage

Profiling and monitoring can be done:

1) With unmodified code:  `python -m palanteer [options] <your script>`

     This syntax is similar to the `cProfile` usage and no script modification is required. <br/>
     By default, it tries to connect to a Palanteer server. With options, offline profiling can be selected. <br/>
     Launch `python -m palanteer` for help or refer to the [documentation](https://dfeneyrou.github.io/palanteer/instrumentation_api_python.md.html#pythoninstrumentationapi/initialization/automaticinstrumentationwithoutcodemodification).

2) With code instrumentation:

    Please refer to the [documentation](https://dfeneyrou.github.io/palanteer/instrumentation_api_python.md.html) for details. <br/>
    Manual instrumentation can provide additional valuable information compared to just the automatic function profiling, like data, locks, ...

The manual instrumentation may also include remotely callable commands (Command Line Interface, aka CLI), useful for configuration and testing.

Here is a working example:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ python
#! /usr/bin/env python3
import sys
import random
from palanteer import *

globalMinValue, globalMaxValue =  0, 10

# Handler (=implementation) of the example CLI, which sets the range
def setBoundsCliHandler(minValue, maxValue):              # 2 parameters (both integer) as declared
    global globalMinValue, globalMaxValue
    if minValue>maxValue:                                 # Case where the CLI execution fails (non null status). The text answer contains some information about it
        return 1, "Minimum value (%d) shall be lower than the maximum value (%d)" % (minValue, maxValue)

    # Modify the state of the program
    globalMinValue, globalMaxValue = minValue, maxValue
    # CLI execution was successful (null status)
    return 0, ""


def main(argv):
    global globalMinValue, globalMaxValue

    plInitAndStart("example")                             # Start the instrumentation
    plDeclareThread("Main")                               # Declare the current thread as "Main", so that it can be identified more easily in the script
    plRegisterCli(setBoundsCliHandler, "config:setRange", "min=int max=int", "Sets the value bounds of the random generator")  # Declare the CLI
    plFreezePoint()                                       # Add a freeze point here to be able to configure the program at a controlled moment

    plBegin("Generate some random values")
    for i in range(100000):
        value = int(globalMinValue + random.random()*(globalMaxValue+1-globalMinValue))
        plData("random data", value)                      # Here are the "useful" values
    plEnd("")                                             # Shortcut for plEnd("Generate some random values")

    plStopAndUninit()                                     # Stop and uninitialize the instrumentation

# Bootstrap
if __name__ == "__main__":
    main(sys.argv)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


## Installation of the instrumentation module

**Latest official release directly from the PyPI storage (from sources on Linux, binary on Windows)**
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
pip install palanteer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Directly from GitHub sources (top of tree, may be unstable)**
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
pip install "git+https://github.com/dfeneyrou/palanteer#egg=palanteer&subdirectory=python"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**From locally retrieved sources**

This method ensures that the viewer and scripting module are consistent with the intrumentation libraries.

Get the sources:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
git clone https://github.com/dfeneyrou/palanteer
cd palanteer
mkdir build
cd build
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Build on Linux:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) install
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Build on Windows:<br/>
(`vcvarsall.bat` or equivalent shall be called beforehand, so that the MSVC compiler is accessible)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
cmake .. -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
nmake install
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


## Important!

To be useful, this module requires at least a "server side":
 - the **[graphical viewer](https://github.com/dfeneyrou/palanteer/releases)**
   - for visual analysis (online or offline) of the collected events
 - the Python **scripting module** [`palanteer_scripting`](https://pypi.org/project/palanteer_scripting)
   - for automated remote usage of the collected events: KPI extraction, tests, monitoring...

NOTE 1: Installing from local sources provide all components: instrumentation module, scripting module, graphical viewer, test examples and documentation.

NOTE 2: It is **strongly** recommended to have matching versions between the server and the instrumentation sides
