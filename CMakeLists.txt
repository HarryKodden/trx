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

# Packaging configuration
set(CPACK_PACKAGE_NAME "trx")
set(CPACK_PACKAGE_VERSION "1.0.1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "TRX - Transaction Processing Language")
set(CPACK_PACKAGE_VENDOR "Harry Kodden")
set(CPACK_PACKAGE_CONTACT "harry@kodden.nl")

# Create tar.gz package
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")

# Include the binary in the package
install(TARGETS trx
        RUNTIME DESTINATION bin)

include(CPack)
