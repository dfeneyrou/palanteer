# ======================
# Palanteer test program
# ======================
# Its purpose is both:
#  - showing a documented instrumented program using Palanteer
#  - be the test program for the full solution

# Requires C++11 or above
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library dependencies
# ====================
if(WIN32)
  # Windows: all libraries are built-in
  add_compile_options(/DUNICODE) # Unicode app
  if(NOT ${CUSTOM_FLAGS} MATCHES ".*PL_IMPL_STACKTRACE=0.*")
    message("Palanteer feature 'stacktrace' enabled for testprogram")
    add_definitions(-DPL_IMPL_STACKTRACE=1)
  endif()
else(WIN32)
  if(NOT CUSTOM_FLAGS MATCHES ".*PL_IMPL_STACKTRACE=0.*")
    # Linux: if libunwind and libdw are present, the stacktrace feature is activated
    find_package(LibUnwind)
    find_package(LibDw)
    if (LIBUNWIND_FOUND AND LIBDW_FOUND)
      add_definitions(-DPL_IMPL_STACKTRACE=1)
      message("Palanteer feature 'stacktrace' enabled for testprogram")
      set(STACKTRACE_LIBS ${LIBUNWIND_LIBRARY} ${LIBDW_LIBRARY} )
    endif()
  endif()
endif(WIN32)


# Compilation flags
# =================

if(MSVC)
  # I gave up trying to have a nice way to force /W4 and /GR-
  add_compile_options(/wd4244 /wd4267 /wd4305 /wd4308) # Disable all the type conversion warnings
  add_compile_options(/wd4996) # Disable the "This function or variable may be unsafe", pushing for not well supported extensions
  add_compile_options(/EHsc)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Add user flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CUSTOM_FLAGS}")

# Test program executable
# =======================
add_executable            ("testprogram"                    testProgram.cpp testPart.cpp testPart.h)
target_link_libraries     ("testprogram" ${STACKTRACE_LIBS} Threads::Threads libpalanteer)

if(NOT CUSTOM_FLAGS MATCHES ".*USE_PL=0.*")
  target_compile_options    ("testprogram" PRIVATE            -DUSE_PL=1)
endif()

# Test program executable with deactivated Palanteer
# =======================
add_executable            ("testprogram_disabled"                    testProgram.cpp testPart.cpp testPart.h)
target_link_libraries     ("testprogram_disabled" ${STACKTRACE_LIBS} Threads::Threads libpalanteer)
target_include_directories("testprogram_disabled" PRIVATE            ../../c++)

