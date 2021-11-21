cmake_minimum_required(VERSION 3.11)
project(palanteer VERSION 1.0.0 DESCRIPTION "Palanteer")

# Options

# All targets are not required for all usages
#  * C++ developer who does not plan to script   : only the viewer (as the C++ instrumentation library is a header file). No Python required.
#  * C++ developer who wants to script only      : only the python scripting package
#  * Python developer who does not plan to script: the viewer and the python instrumentation package
#  * Python developer who wants to script only   : the python scripting package and the python instrumentation package

option(PALANTEER_BUILD_VIEWER "Build the Server/Viewer" ON)
option(PALANTEER_BUILD_CPP_EXAMPLE "Build the C++ example program" ON)
option(PALANTEER_BUILD_PYTHON_INSTRUMENTATION "Build the python instrumentation" ON)
option(PALANTEER_BUILD_SERVER_SCRIPTING "Build the python scripting package" ON)

# Policies
cmake_policy(SET CMP0009 NEW) # For GLOB_RECURSE
cmake_policy(SET CMP0072 NEW) # For OpenGL (Linux)

# Use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif(CCACHE_PROGRAM)

# Add the ASAN target
set(CMAKE_CXX_FLAGS_ASAN "-Wall -fsanitize=address -fno-omit-frame-pointer -g" CACHE STRING
 "Flags used by the C++ compiler during asan builds."  FORCE)
set(CMAKE_C_FLAGS_ASAN "-Wall -fsanitize=address -fno-omit-frame-pointer -g" CACHE STRING
  "Flags used by the C compiler during asan builds."  FORCE)
set(CMAKE_EXE_LINKER_FLAGS_ASAN
  "-fsanitize=address" CACHE STRING  "Flags used for linking binaries during asan builds."  FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_ASAN
  "-fsanitize=address" CACHE STRING  "Flags used by the shared libraries linker during asan builds."  FORCE)
mark_as_advanced(CMAKE_CXX_FLAGS_ASAN CMAKE_C_FLAGS_ASAN CMAKE_EXE_LINKER_FLAGS_ASAN CMAKE_SHARED_LINKER_FLAGS_ASAN)
set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
  "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel Asan." FORCE)

# Built type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Type of build" FORCE)
endif()
message("Build type: ${CMAKE_BUILD_TYPE}   (change with -DCMAKE_BUILD_TYPE=<Debug|Release|RelWithDebInfo|MinSizeRel|Asan>)")
message("Custom configuration flags can be passed with -DCUSTOM_FLAGS=\"-D<name1>=value1 -D<name2>=value2 ...\"")

# Store output in an easy location
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


# System flags
# ============

# Register our package finders
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

# Multithreading is used (both client and server applications)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


# Palanteer
# ==========

# Build libpalanteer
add_subdirectory(c++)

# Build the viewer
if (PALANTEER_BUILD_VIEWER)
	add_subdirectory(server/viewer)
endif()

# Build the C++ example
if (PALANTEER_BUILD_CPP_EXAMPLE)
	add_subdirectory(c++/testprogram)
endif()

# Build the python instrumentation package
if (PALANTEER_BUILD_PYTHON_INSTRUMENTATION)
	add_subdirectory(python)
endif()

# Build the python scripting package
if (PALANTEER_BUILD_SERVER_SCRIPTING)
	add_subdirectory(server/scripting)
endif()
