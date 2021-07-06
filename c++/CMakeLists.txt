# Requires C++11 or above
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_library(libpalanteer INTERFACE)
target_include_directories(libpalanteer INTERFACE ./)
