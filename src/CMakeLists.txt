add_library(trx_core)

# Find SQLite3
find_package(SQLite3 REQUIRED)

# Find ODBC (optional)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ODBC odbc)

# Find PostgreSQL (optional)
pkg_check_modules(POSTGRESQL libpq)

# Find curl (required for HTTP client)
find_package(CURL REQUIRED)

find_package(BISON 3.7 REQUIRED)
find_package(FLEX REQUIRED)

include(CheckIncludeFileCXX)

# Check for ODBC headers
if(ODBC_FOUND)
    check_include_file_cxx(sql.h HAVE_SQL_H)
    if(HAVE_SQL_H)
        set(ODBC_SUPPORTED TRUE)
    endif()
endif()

# Check for PostgreSQL headers
if(POSTGRESQL_FOUND)
    check_include_file_cxx(postgresql/libpq-fe.h HAVE_LIBPQ_FE_H)
    if(HAVE_LIBPQ_FE_H)
        set(POSTGRESQL_SUPPORTED TRUE)
    endif()
endif()

target_sources(trx_core
  PRIVATE
    ast/Module.cpp
    ast/Expressions.cpp
    diagnostics/DiagnosticEngine.cpp
    parsing/ParserDriver.cpp
    parsing/ParserHelpers.cpp
    runtime/Interpreter.cpp
    runtime/SymbolTable.cpp
    runtime/JsonValue.cpp
    runtime/DatabaseDriverFactory.cpp
    runtime/SQLiteDriver.cpp
    runtime/ThreadPool.cpp
)

# Add optional database drivers
if(ODBC_SUPPORTED)
    target_sources(trx_core PRIVATE runtime/ODBCDriver.cpp)
    target_compile_definitions(trx_core PRIVATE HAVE_ODBC)
endif()

if(POSTGRESQL_SUPPORTED)
    target_sources(trx_core PRIVATE runtime/PostgreSQLDriver.cpp)
    target_compile_definitions(trx_core PRIVATE HAVE_POSTGRESQL)
endif()

BISON_TARGET(TRXParser
  ${CMAKE_CURRENT_SOURCE_DIR}/../tools/grammar/trx_parser.y
  ${CMAKE_CURRENT_BINARY_DIR}/trx_parser.cpp
  DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/trx_parser.hpp
)

FLEX_TARGET(TRXLexer
  ${CMAKE_CURRENT_SOURCE_DIR}/../tools/grammar/trx_lexer.l
  ${CMAKE_CURRENT_BINARY_DIR}/trx_lexer.cpp
)

ADD_FLEX_BISON_DEPENDENCY(TRXLexer TRXParser)

target_sources(trx_core
  PRIVATE
    ${BISON_TRXParser_OUTPUTS}
    ${FLEX_TRXLexer_OUTPUTS}
)

target_include_directories(trx_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(ODBC_SUPPORTED)
    target_include_directories(trx_core PRIVATE ${ODBC_INCLUDE_DIRS})
endif()

if(POSTGRESQL_SUPPORTED)
    target_include_directories(trx_core PRIVATE ${POSTGRESQL_INCLUDE_DIRS})
endif()

target_compile_definitions(trx_core
  PRIVATE
    YY_NO_UNISTD_H
    FMT_HEADER_ONLY
)

if(TARGET FLEX::Flex)
  target_link_libraries(trx_core PRIVATE FLEX::Flex)
elseif(FLEX_LIBRARIES)
  target_link_libraries(trx_core PRIVATE ${FLEX_LIBRARIES})
endif()

target_link_libraries(trx_core PRIVATE SQLite::SQLite3)
if(ODBC_SUPPORTED)
    target_link_libraries(trx_core PRIVATE ${ODBC_LIBRARIES})
endif()
if(POSTGRESQL_SUPPORTED)
    target_link_libraries(trx_core PRIVATE ${POSTGRESQL_LIBRARIES})
endif()
target_link_libraries(trx_core PRIVATE CURL::libcurl)

add_executable(trx
  cli/main.cpp
  cli/Server.cpp
)

target_link_libraries(trx
  PRIVATE
    trx_core
)
