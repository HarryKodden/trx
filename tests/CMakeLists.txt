add_library(trx_test_support INTERFACE)

target_link_libraries(trx_test_support
  INTERFACE
    trx_core
)

add_executable(trx_assignment_test
  runtime/AssignmentTest.cpp
)

target_link_libraries(trx_assignment_test
  PRIVATE
    trx_core
)

add_test(
  NAME AssignmentTest
  COMMAND trx_assignment_test
)
