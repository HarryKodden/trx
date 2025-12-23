add_library(trx_core)

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
    runtime/PostgreSQLDriver.cpp
    runtime/ODBCDriver.cpp
)

# Add optional database drivers
if(ODBC_FOUND)
    target_compile_definitions(trx_core PRIVATE HAVE_ODBC)
endif()

if(POSTGRESQL_FOUND)
    target_compile_definitions(trx_core PRIVATE HAVE_POSTGRESQL)
endif()

target_include_directories(trx_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

find_package(BISON 3.7 REQUIRED)
find_package(FLEX REQUIRED)

# Find SQLite3
find_package(SQLite3 REQUIRED)

# Find ODBC (optional)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ODBC odbc)
if(ODBC_FOUND)
    target_link_libraries(trx_core PRIVATE ${ODBC_LIBRARIES})
    target_include_directories(trx_core PRIVATE ${ODBC_INCLUDE_DIRS})
    target_compile_definitions(trx_core PRIVATE HAVE_ODBC)
endif()

# Find PostgreSQL (optional)
pkg_check_modules(POSTGRESQL libpq)
if(POSTGRESQL_FOUND)
    target_link_libraries(trx_core PRIVATE ${POSTGRESQL_LIBRARIES})
    target_include_directories(trx_core PRIVATE ${POSTGRESQL_INCLUDE_DIRS})
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
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

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
if(ODBC_FOUND)
    target_link_libraries(trx_core PRIVATE ${ODBC_LIBRARIES})
endif()
if(POSTGRESQL_FOUND)
    target_link_libraries(trx_core PRIVATE ${POSTGRESQL_LIBRARIES})
endif()

add_executable(trx_compiler
  cli/main.cpp
  cli/SwaggerServer.cpp
)

target_link_libraries(trx_compiler
  PRIVATE
    trx_core
)
