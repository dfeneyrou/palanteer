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
  if(NOT ${CUSTOM_FLAGS} MATCHES ".*PL_IMPL_STACKTRACE=0.*")
    message("Palanteer feature 'stacktrace' enabled for testprogram")
    add_definitions(-DPL_IMPL_STACKTRACE=1)
  endif()
else(WIN32)
  if(NOT CUSTOM_FLAGS MATCHES ".*PL_IMPL_STACKTRACE=0.*")
    # Linux: if libunwind and libdw are present, the stacktrace feature is activated
    find_package(LibUnwind)
    find_package(LibDw)
    if (LibUnwind_FOUND AND LibDw_FOUND)
      add_definitions(-DPL_IMPL_STACKTRACE=1)
      message("Palanteer feature 'stacktrace' enabled for testprogram")
      set(STACKTRACE_LIBS ${LibUnwind_LIBRARY} ${LibDw_LIBRARY} )
    endif()
  endif()
endif(WIN32)


# Compilation flags
# =================

if(MSVC)
  add_compile_options(/DUNICODE) # Unicode app
  add_compile_options(/W4 /permissive-)
  add_compile_options(/wd4996) # Disable the "This function or variable may be unsafe", pushing for not well supported extensions
  add_compile_options(/wd4324) # Disable the "structure was padded due to alignment specifier". Yes, we use alignas(), no problem with that
  add_compile_options(/wd4201) # Disable the "nonstandard extension used: nameless struct/union"
  add_compile_options(/wd4127) # Disable the "conditional expression is constant" warning, applicable only from C++17
  add_compile_options(/EHsc)
else()
  add_compile_options(-Wall -Wextra)

  # Auto instrumentation.
  # This code below is just for the testprogram. A real use-case would be to set these flags all together, including -DPL_IMPL_AUTO_INSTRUMENT=1
  if(${CUSTOM_FLAGS} MATCHES ".*PL_IMPL_AUTO_INSTRUMENT=1.*")
    add_compile_options(-g -finstrument-functions -finstrument-functions-exclude-file-list=palanteer.h,include/c++ -finstrument-functions-exclude-function-list=__tls_init)
    set(DYNLIB_LIBS dl)
  endif()
endif()

# Add user flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CUSTOM_FLAGS}")

set(TESTPROGRAM_SRC testProgram.cpp testPart.cpp testPart.h)


# Test program executable
# =======================
add_executable            ("testprogram" ${TESTPROGRAM_SRC})
target_link_libraries     ("testprogram" libpalanteer ${STACKTRACE_LIBS} ${DYNLIB_LIBS} Threads::Threads)
target_compile_options    ("testprogram" PRIVATE -Wno-mismatched-new-delete)

if(NOT CUSTOM_FLAGS MATCHES ".*USE_PL=0.*")
  target_compile_options    ("testprogram" PRIVATE            -DUSE_PL=1)
endif()

# Command which builds the lookup from the sources (external strings feature) and the binary (auto instrumentation feature)
# As it is a demo, the lookup contains the informations for both cases. It also does not take into account a different hash salt nor the
# 32 bit hash case (all this would translate into additional parameter to the python lookup generation tool)
add_custom_command(TARGET "testprogram" POST_BUILD
  COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/stringLookupGenerator.py" --exe "C++ example" "$<TARGET_FILE:testprogram>" ${TESTPROGRAM_SRC} > "$<TARGET_FILE:testprogram>.txt"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Generating the string lookup..."
)
