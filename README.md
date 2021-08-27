# Look into Palanteer and get an omniscient view of your program

Palanteer is a set of lean and efficient tools to improve the quality of software, for C++ and Python programs.

Simple code instrumentation, mostly automatic in Python, delivers powerful features:
  - **Collection of meaningful atomic events** on timings, memory, locks wait and usage, context switches, data values..
  - **Visual and interactive observation** of records: hierarchical logs, timeline, plot, histogram, flame graph...
  - **Remote command call and events observation can be scripted in Python**: deep testing has never been simpler
  - **C++**:
    - ultra-light single-header cross-platform instrumentation library
    - compile-time selection of groups of instrumentation
    - compile-time hashing of static strings to minimize their cost
    - compile-time striping of all instrumentation static strings
    - enhanced assertions, stack trace dump...
  - **Python**:
    - Automatic instrumentation of functions enter/leave, memory allocations, raised exceptions, garbage collection runs
    - Seamless support of multithreading, asyncio/gevent

<img src="docs/images/views.gif " alt="Palanteer viewer image" width="1000"/>

Palanteer is an efficient, lean and comprehensive solution for better and enjoyable software development!

## C++ instrumentation example

Below is a simple example of a C++ program instrumented with Palanteer and generating 100 000 random integers.
The range can be remotely configured with a user-defined CLI.

The Python scripting module can control this program, in particular:
   - call the setBoundsCliHandler to change the configuration
   - temporarily stop the program at the freeze point
   - see all "random data" values and the timing of the scope event "Generate some random values"

<details>
  <summary> See C++ example code </summary>

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ c++
// File: example.cpp
// On Linux, build with:  g++ -DUSE_PL=1 -I <palanteer C++ instrumentation folder> example.cpp -lpthread -o example
#include <stdlib.h>          // For "rand"
#define PL_IMPLEMENTATION 1  // The instrumentation library shall be "implemented" once
#include "palanteer.h"

int globalMinValue = 0, globalMaxValue = 10;

// Handler (=user implementation) of the example CLI, which sets the range
void setBoundsCliHandler(plCliIo& cio)             // 'cio' is a communication helper passed to each C++ CLI handler
{
    int minValue = cio.getParamInt(0);             // Get the 2 CLI parameters as integers (as declared)
    int maxValue = cio.getParamInt(1);
    if(minValue>maxValue) {                        // Case where the CLI execution fails. The text answer contains some information about it
        cio.setErrorState("Minimum value (%d) shall be lower than the maximum value (%d)", minValue, maxValue);
        return;
    }

    // Modify the state of the program. No care about thread-safety here, to keep the example simple
    globalMinValue = minValue;
    globalMaxValue = maxValue;
    // CLI execution was successful (because no call to cio.setErrorState())
}


int main(int argc, char** argv)
{
    plInitAndStart("example");              // Start the instrumentation, for the program named "example"
    plDeclareThread("Main");                // Declare the current thread as "Main" so that it can be identified more easily in the script
    plRegisterCli(setBoundsCliHandler, "config:setRange", "min=int max=int", "Sets the value bounds of the random generator");  // Declare our CLI
    plFreezePoint();                        // Add a freeze point here to be able to configure the program at a controlled moment

    plBegin("Generate some random values");
    for(int i=0; i<100000; ++i) {
        int value = globalMinValue + rand()%(globalMaxValue+1-globalMinValue);
        plData("random data", value);       // Here are the "useful" values
    }
    plEnd("");                              // Shortcut for plEnd("Generate some random values")

    plStopAndUninit();                      // Stop and uninitialize the instrumentation
    return 0;
}
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

</details>

Some C++ performance figures (see [here](https://dfeneyrou.github.io/palanteer/index.html#performance) for more details):
  - nanosecond resolution and ~25 nanoseconds cost per event on a standard x64 machine
  - up to ~5 millions events per second when recording, bottleneck on the server processing side
  - up to ~150 000 events per second when processing the flow through a Python script, bottleneck on the Python script side

## Python instrumentation example

Execution of unmodified Python programs can be analyzed directly with a syntax similar to the one of `cProfile`, as a large part of the instrumentation is automated by default:
   - Functions enter/leave
   - Interpreter memory allocations
   - All raised exceptions
   - Garbage collection runs
   - Coroutines

In some cases, a manual instrumentation which enhances or replaces the automatic one is desired. <br/>
The example below is an equivalent of the C++ code above, but in Python:

<details>
  <summary> See Python manual instrumentation example code </summary>

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

</details>


## Scripting example

Both examples above (C++ and Python) can be remotely controlled with a simple Python script.

Typical usages are:
  - Tests based on stimulations/configuration with CLI and events observation, as data can also be traced
  - Evaluation of the program performance
  - Monitoring
  - ...

<details>
  <summary> See a scripting example code (Python) </summary>

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

</details>

The execution of this last script, with the compile C++ as parameter, gives the following output:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
> time ./remoteScript.py example
Quantity: 100000
Minimum : 300
Average : 400
Maximum : 500
./remoteScript.py example  0.62s user 0.02s system 24% cpu 2.587 total
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Details can be found [here](https://dfeneyrou.github.io/palanteer/index.html#overview/commonfeatures/remotecontrol).

## Documentation

The complete documentation is accessible inside the repository, and online:
  - [Introduction](https://dfeneyrou.github.io/palanteer/index.html)
  - [Getting started](https://dfeneyrou.github.io/palanteer/getting_started.md.html)
  - [Base concepts](https://dfeneyrou.github.io/palanteer/base_concepts.md.html)
  - [C++ instrumentation API](https://dfeneyrou.github.io/palanteer/instrumentation_api_cpp.md.html)
  - [C++ instrumentation configuration](https://dfeneyrou.github.io/palanteer/instrumentation_configuration_cpp.md.html)
  - [Python instrumentation API](https://dfeneyrou.github.io/palanteer/instrumentation_api_python.md.html)
  - [Scripting API](https://dfeneyrou.github.io/palanteer/scripting_api.md.html)
  - [More](https://dfeneyrou.github.io/palanteer/more.md.html)

## OS Support

Viewer and scripting library:
  - Linux 64 bits
  - Windows 10

Instrumentation libraries:
   - Linux 32 or 64 bits (tested on PC and armv7l)
   - Windows 10
   - Support for virtual threads
     - in [C++](https://dfeneyrou.github.io/palanteer/instrumentation_api_cpp.md.html#c++instrumentationapi/virtualthreads) (userland threads, like fibers)
     - in [Python](https://dfeneyrou.github.io/palanteer/instrumentation_api_python.md.html#virtualthreads) (asyncio / gevent)

## Requirements

Palanteer is lean, its full installation requires only usual components:
  - a C++14+ compiler (gcc, clang or MSVC) in Windows 10 or Linux 64 bits for the viewer and scripting module
  - a C++11+ compiler (tested with gcc, clang and MSVC) 32 or 64 bits for the C++ instrumentation library
  - CPython 3.7+
  - OpenGL 3.3+

In particular, the C++ single-header instrumentation library requires only C++11 or above.

See [here](https://dfeneyrou.github.io/palanteer/index.html#requirements) for more details on the requirements per component.

Other dependencies are snapshotted inside this repository, so for information only:

| Dependency name                  | License type                | URL                                            |
|----------------------------------|-----------------------------|------------------------------------------------|
| Khronos OpenGL API and Extension | MIT                         | https://www.khronos.org/registry/OpenGL/api/GL |
| Dear ImGui                       | MIT                         | https://github.com/ocornut/imgui               |
| stb_image                        | Public domain               | https://github.com/nothings/stb                |
| Fonts 'Roboto-Medium.ttf'        | Apache License, Version 2.0 | https://fonts.google.com/specimen/Roboto       |
| ZStandard                        | BSD                         | https://facebook.github.io/zstd                |
| Markdeep                         | BSD                         | https://casual-effects.com/markdeep            |


## License

The instrumentation libraries are under the MIT license.

The viewer and the Python scripting module are under the AGPLv3+.

See [LICENSE.md](LICENSE.md) for details.


## Warning: Beta state

Even if no major bugs are known and special care has been taken to test as many cases as possible, this project is young and in beta state.

Your feedback and raised issues are warmly welcome to improve its quality, especially as it aims at improving software quality...

The state of the "interfaces" is:
  - **Instrumentation API**: stable, no big changes planned
  - **Client-server protocol**: still evolving. The induced constraint is that the servers and instrumentation libraries shall match.
  - **Record storage**: still evolving. The impact of compatibility breaks is that older records cannot be read anymore.

Interface stability and support of older versions is planned when the project is more mature. At the moment, such constraint would clamp down on its evolution.
