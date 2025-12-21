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
)

target_include_directories(trx_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

find_package(BISON 3.7 REQUIRED)
find_package(FLEX REQUIRED)

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

add_executable(trx_compiler
  cli/main.cpp
  cli/SwaggerServer.cpp
)

target_link_libraries(trx_compiler
  PRIVATE
    trx_core
)
