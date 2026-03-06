option(BUILD_TESTS_HOST "Build host unit tests" OFF)
option(BUILD_TESTS_TARGET "Build on-target tests" OFF)

if(BUILD_TESTS_HOST)
  message(STATUS "⚙️ Configuring host unit-tests..")
  add_subdirectory(host)
  return()
endif()

if(BUILD_TESTS_TARGET)
  message(STATUS "⚙️ Configuring on-target unit-tests..")
  add_subdirectory(target)
  return()
endif()
