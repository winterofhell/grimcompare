add_executable(gdc_tests
  main.cpp
  test_pe_exports.cpp
  test_hook_targets.cpp
  test_real_binaries.cpp
)
target_link_libraries(gdc_tests PRIVATE gdc_core)
target_include_directories(gdc_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_compile_definitions(gdc_tests PRIVATE GDC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME gdc_tests COMMAND gdc_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME launch_options
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_launch_options.py -v)
set_tests_properties(launch_options PROPERTIES ENVIRONMENT "GDC_SCRIPTS=${PROJECT_SOURCE_DIR}/scripts")
