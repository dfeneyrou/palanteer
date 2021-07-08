Global installation
===================

## Clone the GIT repository

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
git clone https://github.com/dfeneyrou/palanteer
cd palanteer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## Build and install all components

Requires CMake, gcc/MSVC, Python 3.7+ with pysetuptools and wheel.

### On Linux

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) install
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Output:
  - `./bin/palanter` (viewer)
  - `./bin/testprogram` (C++ example program)
  - the installation of the Python module `palanteer` (Python instrumentation)
  - the installation of the Python module `palanteer_scripting` (scripting module)

The "install" target builds all components (as `make` would do) and additionaly installs the 2 Python built `wheel` packages (globally if root, else locally).

### On Windows

`vcvarsall.bat` or equivalent shall be called beforehand, so that the MSVC compiler is accessible.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
nmake install
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Output:
  - `bin\palanter.exe` (viewer)
  - `bin\testprogram.exe` (C++ example program)
  - the installation of the Python module `palanteer` (Python instrumentation)
  - the installation of the Python module `palanteer_scripting` (scripting module)

The "install" target builds all components (as `make` would do) and additionaly installs the 2 built `wheel` Python packages (globally if administrator, else locally).


## Deactivating some components

All components of Palanteer are usually not required. <br/>
Some typical usages are:
  - A C++ developer who does not plan to script
    - Only the viewer, as the C++ instrumentation library is a header file
    - No Python required.
  - A C++ developer who wants to script only
    - Only the python scripting package, as the C++ instrumentation library is a header file
  - A Python developer who does not plan to script
    - The viewer
    - The python instrumentation package
  - A Python developer who wants to script only
    - The python scripting package
    - The python instrumentation package

Such roles, or a mix of them, can be enforced with the following CMake options (to use with `-D<option>=<ON|OFF>` at configuration time, ON as a default):
  - `PALANTEER_BUILD_VIEWER`
  - `PALANTEER_BUILD_CPP_EXAMPLE`
  - `PALANTEER_BUILD_PYTHON_INSTRUMENTATION`
  - `PALANTEER_BUILD_SERVER_SCRIPTING`

Example:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
cmake .. -DCMAKE_BUILD_TYPE=Release -DPALANTEER_BUILD_CPP_EXAMPLE=OFF -DPALANTEER_BUILD_SERVER_SCRIPTING=OFF
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
