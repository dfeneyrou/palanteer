# Find the libunwind library
#
#  LibUnwind_FOUND       - True if libunwind was found.
#  LibUnwind_LIBRARIES   - The libraries needed to use libunwind
#  LibUnwind_INCLUDE_DIR - Location of unwind.h and libunwind.h

# Find includes and lib
find_path(LibUnwind_INCLUDE_DIR NAMES libunwind.h)
if(NOT LibUnwind_INCLUDE_DIR)
  message(STATUS "failed to find libunwind.h")
endif()

# Find library
find_library(LibUnwind_LIBRARY NAMES unwind)
if(NOT LibUnwind_LIBRARY)
    MESSAGE(STATUS "failed to find libunwind library")
  endif()

# Some caching
mark_as_advanced(LibUnwind_INCLUDE_DIR  LibUnwind_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibUnwind  DEFAULT_MSG    LibUnwind_LIBRARY  LibUnwind_INCLUDE_DIR)
