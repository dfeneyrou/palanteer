# ================
# Palanteer viewer
# ================

# Requires C++14 (for list initialization)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Library dependencies
# ====================

if(WIN32)
  # Windows: all libraries are built-in
  add_compile_options(/DUNICODE) # Unicode app
  if(NOT ${CUSTOM_FLAGS} MATCHES ".*PL_IMPL_STACKTRACE=0.*")
    message("Palanteer feature 'stacktrace' enabled for viewer")
    add_definitions(-DPL_IMPL_STACKTRACE=1)
  endif()
else(WIN32)
  # Linux: openGL and X11 are required
  find_package(OpenGL REQUIRED)
  find_package(X11 REQUIRED)
  if(NOT X11_Xrender_FOUND)
    message(FATAL_ERROR "XRender library has not been found")
  endif()
  if(NOT CUSTOM_FLAGS MATCHES ".*PL_IMPL_STACKTRACE=0.*")
    # If libunwind and libdw are present, the stacktrace feature is activated
    find_package(LibUnwind)
    find_package(LibDw)
    if (LIBUNWIND_FOUND AND LIBDW_FOUND)
      add_definitions(-DPL_IMPL_STACKTRACE=1)
      message("Palanteer feature 'stacktrace' enabled for viewer")
      set(STACKTRACE_LIBS ${LIBUNWIND_LIBRARY} ${LIBDW_LIBRARY} )
    endif()
  endif()
  set(PALANTEER_LIBS ${STACKTRACE_LIBS} OpenGL::GL ${X11_LIBRARIES} ${X11_Xrender_LIB})
endif(WIN32)


# Compilation flags
# =================
add_definitions(-DPL_EXPORT=1) # Required to get infos from the instrumentation library
add_definitions(-DUSE_PL=1 -DPL_NOCONTROL=1 -DPL_NOEVENT=1) # Optional: activates Palanteer, at least for the assertions

# Additional checks in debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-DWITH_GL_CHECK=1 -DPL_NO_COMPRESSION)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")
if(CMAKE_BUILD_TYPE STREQUAL "Asan")
  add_compile_options(-DWITH_GL_CHECK=1 -DPL_NO_COMPRESSION)
endif(CMAKE_BUILD_TYPE STREQUAL "Asan")

if(MSVC)
  # I gave up trying to have a nice way to force /W4 and /GR-
  add_compile_options(/wd4244 /wd4267 /wd4305 /wd4308) # Disable all the type conversion warnings
  add_compile_options(/wd4996) # Disable the "This function or variable may be unsafe", pushing for not well supported extensions
  add_compile_options(/wd6385) # Disable the "Reading invalid data from..." which generates mainly obvious false alarms
  add_compile_options(/wd6255) # Disable the "_alloca indicates failure by raising a stack overflow exception.  Consider using _malloca instead"
  add_compile_options(/EHsc)
  set(WIN_MAIN WIN32)
else()
  add_compile_options(-Wall -Wextra)
  set(cxx_flags -fno-rtti -fno-exceptions -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${cxx_flags}>") # We have some C files too (in 3rd party zstd)
endif()

# Add user flags. Palanteer config can be overriden here, at the price of a potential warning (duplicate flags)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CUSTOM_FLAGS}")


# Viewer sources
# ==============
file(GLOB_RECURSE ZSTD_SRC  CONFIGURE_DEPENDS  ../external/zstd/*.c ../external/zstd/*.h)
file(GLOB         IMGUI_SRC CONFIGURE_DEPENDS  ../external/imgui/*.cpp ../external/imgui/*.h)
file(GLOB         BASE_SRC   CONFIGURE_DEPENDS ../base/*.cpp ../base/*.h)
file(GLOB         COMMON_SRC CONFIGURE_DEPENDS ../common/*.cpp ../common/*.h)
file(GLOB         VIEWER_SRC CONFIGURE_DEPENDS ../external/*.h *.cpp *.h)
set(VIEWER_SRC ${VIEWER_SRC} ${COMMON_SRC} ${BASE_SRC} ${ZSTD_SRC} ${IMGUI_SRC})

# Viewer executable
# =================
add_executable("palanteer" ${WIN_MAIN} ${VIEWER_SRC})
target_link_libraries("palanteer" ${PALANTEER_LIBS} Threads::Threads libpalanteer)
target_include_directories("palanteer" PRIVATE
  ../external/zstd ../external/zstd/common ../external/zstd/compress ../external/zstd/decompress
  ../external/imgui ../external ../../c++  ../base ../common)
