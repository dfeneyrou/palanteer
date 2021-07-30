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

## Customized test program

The C++ test program receives customized compilation flags through the variable `CUSTOM_FLAGS`. <br/>
For the list of options, refer to the [instrumentation configuration](https://dfeneyrou.github.io/palanteer/instrumentation_configuration_cpp.md.html).

For instance, the following command builds the testprogram with Palanteer fully disabled (example for Linux):

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
cmake .. -DCUSTOM_FLAGS="-DUSE_PL=0"
make testprogram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Another example (for Linux), the following command builds the testprogram with Palanteer and:
 - without the memory tracing
 - with the external string feature activated
 - with simple assertions

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
cmake .. -DCUSTOM_FLAGS="-DPL_IMPL_OVERLOAD_NEW_DELETE=0 -DPL_EXTERNAL_STRINGS=1 -DPL_SIMPLE_ASSERT=1"
make testprogram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

NOTE: Beware that the `CUSTOM_FLAGS` value is persistent with cmake. To clear it, simply use ```-DCUSTOM_FLAGS=""``` in a cmake configuration call

## Manual installation of Python modules

Calling ```make``` without the "install" target just builds some components. <br/>
The two Python modules (instrumentation and scripting) are generated as `wheel` packages.

They can be installed manually with `pip`:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
pip install python/dist/palanteer-<XXX>-.whl
pip install server/scripting/dist/palanteer_scripting-<XXX>-.whl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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

The persistence of this configuration is ensured by the CMake caching mechanism.
