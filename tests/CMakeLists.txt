add_executable(gdc_tests
  main.cpp
  test_pe_exports.cpp
  test_hook_targets.cpp
  test_real_binaries.cpp
  test_attribute_names.cpp
  test_snapshot.cpp
  test_code_scan.cpp
  test_combat_formulas.cpp
  test_arz.cpp
  test_stat_model.cpp
  test_db_fields.cpp
  test_store.cpp
  test_tooltip.cpp
  test_text.cpp
  test_score.cpp
  test_sets.cpp
  test_granted.cpp
  test_presets.cpp
  test_effects.cpp
  test_utility.cpp
  test_requirements.cpp
  test_enemy.cpp
  test_sanity.cpp
  test_symmetry.cpp
  test_regress.cpp
  test_classes.cpp
  test_templates.cpp
  test_setup.cpp
  test_catalog.cpp
)
target_link_libraries(gdc_tests PRIVATE gdc_core gdc_setup)
target_include_directories(gdc_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_compile_definitions(gdc_tests PRIVATE GDC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME gdc_tests COMMAND gdc_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# tools/snapsum on a store written by gdc::store.
set(GDC_STORE_OUT ${CMAKE_CURRENT_BINARY_DIR}/snapsum-store)
add_test(NAME store_fixture COMMAND gdc_tests "--test-case=store: fixture for tools/snapsum")
set_tests_properties(store_fixture PROPERTIES ENVIRONMENT "GDC_STORE_OUT=${GDC_STORE_OUT}" FIXTURES_SETUP store)
add_test(NAME snapsum COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_snapsum.py -v)
set_tests_properties(snapsum PROPERTIES FIXTURES_REQUIRED store
  ENVIRONMENT "GDC_STORE_OUT=${GDC_STORE_OUT};GDC_SNAPSUM=${PROJECT_SOURCE_DIR}/tools/snapsum")
