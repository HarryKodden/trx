cmake_minimum_required(VERSION 3.22)
project(trx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

add_subdirectory(src)

option(TRX_BUILD_TESTS "Build the TRX unit test suite" ON)
if(TRX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
