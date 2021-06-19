# Look into Palanteer and get an omniscient view of your program

Palanteer is a set of tools to improve the general software quality, for C++ and Python programs.

Simple code instrumentation, partially automatic in Python, delivers powerful features:
  - **Collection of meaningful atomic events** on timings, memory, locks wait and usage, context switches, data values..
  - **Visual and interactive observation** of record: timeline, plot, histograms, flame graph, ...
  - **Remote command call and events observation can be scripted in Python**: deep testing has never been simpler
  - **C++**:
    - ultra-light single-header cross-platform instrumentation library
    - compile-time selection of groups of instrumentation
    - compile-time hashing of static string to minimize their cost
    - compile-time striping of all instrumentation static strings
    - enhanced assertions, stack trace dump...
  - **Python**:
    - Automatic instrumentation of functions enter/leave, memory allocations, raised exceptions, garbage collection runs

<img src="docs/images/views.gif " alt="Palanteer viewer image" width="1000"/>

Palanteer is an efficient, lean and comprehensive solution for better and enjoyable software development!

## C++ instrumentation example

Below is a simple example of a C++ program instrumented with Palanteer and generating 100 000 random integers.
The range can be remotely configured with a user-defined CLI.

The Python scripting module can control this program, in particular:
   - call the setBoundsCliHandler to change the configuration
   - temporarily stop the program at the freeze point
   - see all "random data" values and the timing of the scope event "Generate some random values"

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

Some C++ performance figures (see [here](https://dfeneyrou.github.io/palanteer/index.html#performance) for more details):
  - nanosecond resolution and ~25 nanoseconds cost per event on a standard x64 machine
  - up to ~3 millions events per second when recording, the bottleneck being on the server processing side
  - up to ~150 000 events per second when processing the flow through a Python script, the bottleneck being on the Python script side


## Python instrumentation example

Same example than in C++ but in Python:
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

## Documentation

The complete documentation is accessible inside the repository, and online:
  - [Introduction](https://dfeneyrou.github.io/palanteer/index.html)
  - [Getting started](https://dfeneyrou.github.io/palanteer/getting_started.md.html)
  - [Base concepts](https://dfeneyrou.github.io/palanteer/base_concepts.md.html)
  - [C++ instrumentation API](https://dfeneyrou.github.io/palanteer/instrumentation_api_cpp..md.html)
  - [C++ instrumentation configuration](https://dfeneyrou.github.io/palanteer/instrumentation_configuration_cpp.md.html)
  - [Python instrumentation API](https://dfeneyrou.github.io/palanteer/instrumentation_api_python.md.html)
  - [Scripting API](https://dfeneyrou.github.io/palanteer/scripting_api.md.html)
  - [More](https://dfeneyrou.github.io/palanteer/more.md.html)


## Requirements

Full installation of Palanteer requires:
  - a C++14+ compiler (gcc, clang or MSVC) in Windows 10 or Linux 64 bits for the viewer and scripting module
  - a C++11+ compiler (tested with gcc, clang and MSVC) 32 or 64 bits for the C++ instrumentation library
  - CPython 3.7+  with setuptools package
  - OpenGL 3.3+

See [here](https://dfeneyrou.github.io/palanteer/index.html#requirements) for detailed requirements per component.

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

Even if no major bugs are known and a special care has been taken to test as many cases as possible, this project is young and in beta state.

Your feedback and raised issues are warmly welcome to improve its quality, especially as it aims at improving software quality...
