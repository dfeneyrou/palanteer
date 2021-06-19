# Find the libdw library (from elfutils)
#
#  LIBDW_FOUND       - True if libdw was found.
#  LIBDW_LIBRARIES   - The libraries needed to use libdw
#  LIBDW_INCLUDE_DIR - Location of elfutils/libdwfl.h

# Find includes and lib
find_path(LIBDW_INCLUDE_DIR NAMES elfutils/libdwfl.h)
if(NOT LIBDW_INCLUDE_DIR)
  message(STATUS "failed to find elfutils/libdwfl.h")
endif()

# Find library
find_library(LIBDW_LIBRARY NAMES dw)
if(NOT LIBDW_LIBRARY)
    MESSAGE(STATUS "failed to find libdw library")
  endif()

# Some caching
mark_as_advanced(LIBDW_INCLUDE_DIR  LIBDW_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBDW  DEFAULT_MSG    LIBDW_LIBRARY  LIBDW_INCLUDE_DIR)
