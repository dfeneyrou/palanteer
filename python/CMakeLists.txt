# ================================
# Palanteer instrumentation library
# ================================

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Some definitions
set(SETUP_PY_IN ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in)
set(SETUP_PY    ${CMAKE_CURRENT_BINARY_DIR}/setup.py)
# Allows to easily track "dirtiness" without knowing the name of the real output files
set(FAKE_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/python_instrumentation_install)
# Dependency files
file(GLOB DEPS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in
  ${CMAKE_CURRENT_SOURCE_DIR}/palanteer/*.py
  ${CMAKE_CURRENT_SOURCE_DIR}/../c++/palanteer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/palanteer/_cextension/*.cpp)

# Create the setup.py on the build side (copy of the (manual usage) setup.py but with path automatically set by cmake)
configure_file(${SETUP_PY_IN} ${SETUP_PY})

# Command which build and install the library.
# Officially, it creates an artificial "dependency_timestamp" file, which is easier to track than the real output of the build
add_custom_command(OUTPUT ${FAKE_OUTPUT}
  COMMAND ${Python3_EXECUTABLE} ${SETUP_PY} bdist_wheel
  COMMAND ${CMAKE_COMMAND} -E touch ${FAKE_OUTPUT}
  DEPENDS ${DEPS}
  )

# Add the target
add_custom_target(python_instrumentation ALL DEPENDS ${FAKE_OUTPUT})

# Add the installation command
if(WIN32)
  install(CODE "execute_process(COMMAND ${CMAKE_SOURCE_DIR}/cmake/pip_install_whl_package.bat ${Python3_EXECUTABLE} \"${CMAKE_CURRENT_BINARY_DIR}\")")
else(WIN32)
  install(CODE "execute_process(COMMAND ${CMAKE_SOURCE_DIR}/cmake/pip_install_whl_package.sh ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR})")
endif(WIN32)
