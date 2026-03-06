function(add_host_test TEST_NAME TEST_SOURCES)
  add_executable(${TEST_NAME} ${TEST_SOURCES})

  target_link_libraries(${TEST_NAME} PRIVATE lib_external)

  add_test(
    NAME ${TEST_NAME}
    COMMAND ${TEST_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endfunction()

# Подключаешь тесты лаконично add_host_test( test_host_simple test_host_simple.c
# ${PROJECT_SOURCE_DIR}/lib/hal/src/app.c # тестируемый модуль )

# add_host_test(test_host_second test_host_second.c
# ${PROJECT_SOURCE_DIR}/lib/hal/src/app.c)
