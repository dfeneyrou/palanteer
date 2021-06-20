# ============================
# Palanteer scripting module
# ============================


find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Some definitions
set(SETUP_PY_IN ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in)
set(SETUP_PY    ${CMAKE_CURRENT_BINARY_DIR}/setup.py)
# Allows to easily track "dirtiness" without knowing the name of the real output files
set(FAKE_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/scripting_install)
# Dependency files
file(GLOB_RECURSE ZSTD_SRC   CONFIGURE_DEPENDS ../external/zstd/*.c)
file(GLOB         BASE_SRC   CONFIGURE_DEPENDS ../base/bsString.cpp ../base/bsOsLinux.cpp ../base/bsOsWindows.cpp)
file(GLOB         COMMON_SRC CONFIGURE_DEPENDS ../common/*.cpp)
file(GLOB         PYTHON_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in
  ${CMAKE_CURRENT_SOURCE_DIR}/palanteer_scripting/*.py
  ${CMAKE_CURRENT_SOURCE_DIR}/palanteer_scripting/_cextension/*.cpp)
set(DEPS ${PYTHON_SRC} ${LIB_SRC} ${COMMON_SRC} ${BASE_SRC} ${ZSTD_SRC})

# Create the setup.py on the build side (copy of the (manual usage) setup.py but with path automatically set by cmake)
configure_file(${SETUP_PY_IN} ${SETUP_PY})

# Command which build and install the library.
# Officially, it creates an artificial "dependency_timestamp" file, which is easier to track than the real output of the build
add_custom_command(OUTPUT ${FAKE_OUTPUT}
  COMMAND ${Python3_EXECUTABLE} ${SETUP_PY} install --user
  COMMAND ${CMAKE_COMMAND} -E touch ${FAKE_OUTPUT}
  DEPENDS ${DEPS}
  )

# Add the target
add_custom_target(scripting ALL DEPENDS ${FAKE_OUTPUT})
