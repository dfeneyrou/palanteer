
## Look into Palanteer and get an omniscient view of your program
Palanteer is a set of lean and efficient tools to improve the quality of software, for C++ and Python programs.

Simple code instrumentation, mostly automatic in Python, delivers powerful features:
  - **Collection of meaningful atomic events** on timings, memory, locks wait and usage, context switches, data values..
  - **Visual and interactive observation** of record: timeline, plot, histograms, flame graph...
  - **Remote command call and events observation can be scripted in Python**: deep testing has never been simpler
  - **C++**:
    - ultra-light single-header cross-platform instrumentation library
    - compile-time selection of groups of instrumentation
    - compile-time hashing of static string to minimize their cost
    - compile-time striping of all instrumentation static strings
    - enhanced assertions, stack trace dump...
  - **Python**:
    - Automatic instrumentation of functions enter/leave, memory allocations, raised exceptions, garbage collection runs
    - Support of multithread, coroutines, asyncio/gevent

Palanteer is an efficient, lean and comprehensive solution for better and enjoyable software development!

## Usage

This scripting module processes the events sent by instrumented programs, independently of their language (Python or C++).

Typical usages are:
  - Tests based on stimulations/configuration with CLI and events observation, as data can also be traced
  - Evaluation of the program performance
  - Monitoring
  - ...

Below is a simple example of a Python program instrumented with Palanteer and generating 100 000 random integers. <br/>
The range can be remotely configured with a user-defined `Palanteer` CLI.

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

The Python scripting module can remotely control this program, in particular:
   - call the setBoundsCliHandler to change the configuration
   - temporarily halt the program at the freeze point
   - see all "random data" values and the timing of the scope event "Generate some random values"

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ python
#! /usr/bin/env python3
import sys
import palanteer_scripting as ps

def main(argv):
    if len(sys.argv)<2:
        print("Error: missing parameters (the program to launch)")
        sys.exit(1)

    # Initialize the scripting module
    ps.initialize_scripting()

    # Enable the freeze mode so that we can safely configure the program once stopped on its freeze point
    ps.program_set_freeze_mode(True)

    # Launch the program under test
    ps.process_launch(sys.argv[1], args=sys.argv[2:])
    # From here, we are connected to the remote program

    # Configure the selection of events to receive
    my_selection = ps.EvtSpec(thread="Main", events=["random data"]) # Thread "Main", only the event "random data"
    ps.data_configure_events(my_selection)

    # Configure the program
    status, response = ps.program_cli("config:setRange min=300 max=500")
    if status!=0:
        print("Error when configuring: %s\nKeeping original settings." % response)

    # Disable the freeze mode so that the program resumes its execution
    ps.program_set_freeze_mode(False)

    # Collect the events as long as the program is alive or we got some events in the last round
    qty, sum_values, min_value, max_value, has_worked = 0, 0, 1e9, 0, True
    while ps.process_is_running() or has_worked:
        has_worked = False
        for e in ps.data_collect_events(timeout_sec=1.):  # Loop on received events, per batch
            has_worked, qty, sum_values, min_value, max_value = True, qty+1, sum_values+e.value, min(min_value, e.value), max(max_value, e.value)

    # Display the result of the processed collection of data
    print("Quantity: %d\nMinimum : %d\nAverage : %d\nMaximum : %d" % (qty, min_value, sum_values/max(qty,1), max_value))

    # Cleaning
    ps.process_stop()            # Kills the launched process, if still running
    ps.uninitialize_scripting()  # Uninitialize the scripting module


# Bootstrap
if __name__ == "__main__":
    main(sys.argv)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The execution of this last script, with the first one as parameter, gives the following output:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
> ./remoteScript.py example.py
Quantity: 100000
Minimum : 300
Average : 400
Maximum : 500
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Details can be found [here](https://dfeneyrou.github.io/palanteer/index.html#overview/commonfeatures/remotecontrol).


## Installation of the scripting module

**Directly from the PyPI storage (from sources on Linux, binary on Windows)**
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
pip install palanteer_scripting
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Directly from GitHub sources**
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
pip install "git+https://github.com/dfeneyrou/palanteer#egg=palanteer_scripting&subdirectory=server/scripting"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**From locally retrieved sources**

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

To be useful, this module requires an "instrumentation side" in the program under analysis (C++ or Python):
  - For Python language, the instrumentation module is available [on PyPI](https://pypi.org/project/palanteer) or from the [github sources](https://github.com/dfeneyrou/palanteer)
  - For C++ language, the instrumentation library is a single header file available in the [github sources](https://github.com/dfeneyrou/palanteer)

NOTE: It is **strongly** recommended to have a matching version between the scripting module and the instrumentation sides
