cmake_minimum_required(VERSION 3.25)
project(GDCompare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(core)
add_subdirectory(installer)

if(WIN32)
  add_subdirectory(adapter)
else()
  enable_testing()
  add_subdirectory(tools)
  add_subdirectory(tests)
endif()
