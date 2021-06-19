Global installation
===================

## Clone the GIT repository, if not already done

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
git clone https://github.com/dfeneyrou/palanteer
cd palanteer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## Build all components 

Requires CMake, gcc/MSVC, Python 3.7+ with pysetuptools.

### On Linux

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Output:
  - `./bin/palanter` (viewer)
  - `./bin/testprogram` (example program)
  - the installation of the Python module `palanteer` (Python instrumentation)
  - the installation of the Python module `palanteer_scripting` (scripting module)

### On Windows

`vcvarsall.bat` or equivalent shall be called beforehand, so that the MSVC compiler is accessible.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ shell
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
nmake
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Output:
  - `bin\palanter.exe` (viewer)
  - `bin\testprogram.exe` (example program)
  - the installation of the Python module `palanteer` (Python instrumentation)
  - the installation of the Python module `palanteer_scripting` (scripting module)

### Python module installation for all users

Installation of Python modules through CMake is only for the current user, so that no root / administrator rights are required.

If an installation for all users is desired, the process is to go in each of the folder:

  - `./python` (for the Python instrumentation module)
  - `./server/scripting` (for the Python scripting module)

and run the following command with root / administrator privileges: `python setup.py install`
