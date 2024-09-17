# Find the libdw library (from elfutils)
#
#  LibDw_FOUND       - True if libdw was found.
#  LibDw_LIBRARIES   - The libraries needed to use libdw
#  LibDw_INCLUDE_DIR - Location of elfutils/libdwfl.h

# Find includes and lib
find_path(LibDw_INCLUDE_DIR NAMES elfutils/libdwfl.h)
if(NOT LibDw_INCLUDE_DIR)
  message(STATUS "failed to find elfutils/libdwfl.h")
endif()

# Find library
find_library(LibDw_LIBRARY NAMES dw)
if(NOT LibDw_LIBRARY)
    MESSAGE(STATUS "failed to find libdw library")
  endif()

# Some caching
mark_as_advanced(LibDw_INCLUDE_DIR  LibDw_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibDw  DEFAULT_MSG    LibDw_LIBRARY  LibDw_INCLUDE_DIR)
