enable_language(C)
# miniz 3.0.2 (third_party/miniz/README.md): deflate + crc32 for gdc::store gzip.
add_library(miniz STATIC ${PROJECT_SOURCE_DIR}/third_party/miniz/miniz.c)
target_include_directories(miniz SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/miniz)
target_compile_definitions(miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

add_library(gdc_core STATIC
  src/pe_exports.cpp
  src/hook_targets.cpp
  src/known_builds.cpp
  src/attribute_names.cpp
  src/snapshot.cpp
  src/code_scan.cpp
  src/combat_formulas.cpp
  src/arz.cpp
  src/stat_model.cpp
  src/db_fields.cpp
  src/store.cpp
  src/calibrate.cpp
  src/tooltip.cpp
  src/font.cpp
  src/text.cpp
  src/labels.cpp
  src/components.cpp
  src/requirements.cpp
  src/score.cpp
  src/sets.cpp
  src/effects.cpp
  src/utility.cpp
  src/enemy.cpp
)
target_link_libraries(gdc_core PRIVATE miniz)
target_include_directories(gdc_core PUBLIC include)
target_include_directories(gdc_core SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
if(MSVC)
  target_compile_options(gdc_core PRIVATE /W4)
else()
  target_compile_options(gdc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
