# Find the libunwind library
#
#  LIBUNWIND_FOUND       - True if libunwind was found.
#  LIBUNWIND_LIBRARIES   - The libraries needed to use libunwind
#  LIBUNWIND_INCLUDE_DIR - Location of unwind.h and libunwind.h

# Find includes and lib
find_path(LIBUNWIND_INCLUDE_DIR NAMES libunwind.h)
if(NOT LIBUNWIND_INCLUDE_DIR)
  message(STATUS "failed to find libunwind.h")
endif()

# Find library
find_library(LIBUNWIND_LIBRARY NAMES unwind)
if(NOT LIBUNWIND_LIBRARY)
    MESSAGE(STATUS "failed to find libunwind library")
  endif()

# Some caching
mark_as_advanced(LIBUNWIND_INCLUDE_DIR  LIBUNWIND_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBUNWIND  DEFAULT_MSG    LIBUNWIND_LIBRARY  LIBUNWIND_INCLUDE_DIR)
