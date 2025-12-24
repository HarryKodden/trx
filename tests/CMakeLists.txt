add_library(trx_test_support INTERFACE)

target_link_libraries(trx_test_support
  INTERFACE
    trx_core
)

add_executable(trx_copy_procedure_test
  runtime/TestUtils.h
  runtime/CopyProcedureTest.cpp
)

target_link_libraries(trx_copy_procedure_test
  PRIVATE
    trx_core
)

add_test(
  NAME CopyProcedureTest
  COMMAND trx_copy_procedure_test
)

add_executable(trx_expression_ast_test
  runtime/TestUtils.h
  runtime/ExpressionAstTest.cpp
)

target_link_libraries(trx_expression_ast_test
  PRIVATE
    trx_core
)

add_test(
  NAME ExpressionAstTest
  COMMAND trx_expression_ast_test
)

add_executable(trx_control_statement_test
  runtime/TestUtils.h
  runtime/ControlStatementTest.cpp
)

target_link_libraries(trx_control_statement_test
  PRIVATE
    trx_core
)

add_test(
  NAME ControlStatementTest
  COMMAND trx_control_statement_test
)

add_executable(trx_sql_statement_test
  runtime/TestUtils.h
  runtime/SqlStatementTest.cpp
)

target_link_libraries(trx_sql_statement_test
  PRIVATE
    trx_core
)

add_test(
  NAME SqlStatementTest
  COMMAND trx_sql_statement_test
)

add_executable(trx_exception_handling_test
  runtime/TestUtils.h
  runtime/ExceptionHandlingTest.cpp
)

target_link_libraries(trx_exception_handling_test
  PRIVATE
    trx_core
)

add_test(
  NAME ExceptionHandlingTest
  COMMAND trx_exception_handling_test
)

add_executable(trx_record_format_test
  runtime/TestUtils.h
  runtime/RecordFormatTest.cpp
)

target_link_libraries(trx_record_format_test
  PRIVATE
    trx_core
)

add_test(
  NAME RecordFormatTest
  COMMAND trx_record_format_test
)

add_executable(trx_list_operations_test
  runtime/TestUtils.h
  runtime/ListOperationsTest.cpp
)

target_link_libraries(trx_list_operations_test
  PRIVATE
    trx_core
)

add_test(
  NAME ListOperationsTest
  COMMAND trx_list_operations_test
)

add_executable(trx_switch_statement_test
  runtime/TestUtils.h
  runtime/SwitchStatementTest.cpp
)

target_link_libraries(trx_switch_statement_test
  PRIVATE
    trx_core
)

add_test(
  NAME SwitchStatementTest
  COMMAND trx_switch_statement_test
)

add_executable(trx_cursor_test
  runtime/TestUtils.h
  runtime/CursorTest.cpp
)

target_link_libraries(trx_cursor_test
  PRIVATE
    trx_core
)

add_test(
  NAME CursorTest
  COMMAND trx_cursor_test
)

add_executable(trx_builtin_test
  runtime/TestUtils.h
  runtime/BuiltinTest.cpp
)

target_link_libraries(trx_builtin_test
  PRIVATE
    trx_core
)

add_test(
  NAME BuiltinTest
  COMMAND trx_builtin_test
)

add_executable(trx_http_test
  runtime/TestUtils.h
  runtime/HttpTest.cpp
)

target_link_libraries(trx_http_test
  PRIVATE
    trx_core
)

add_test(
  NAME HttpTest
  COMMAND trx_http_test
)

add_executable(trx_thread_pool_test
  runtime/TestUtils.h
  runtime/ThreadPoolTest.cpp
)

target_link_libraries(trx_thread_pool_test
  PRIVATE
    trx_core
)

add_test(
  NAME ThreadPoolTest
  COMMAND trx_thread_pool_test
)
