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
  src/snapshot_read.cpp
  src/snapshot_write.cpp
  src/code_scan.cpp
  src/combat_formulas.cpp
  src/arz.cpp
  src/stat_model.cpp
  src/stat_model_derive.cpp
  src/stat_model_effects.cpp
  src/stat_model_granted.cpp
  src/stat_model_hidden.cpp
  src/stat_model_skills.cpp
  src/db_fields.cpp
  src/store.cpp
  src/calibrate.cpp
  src/calibrate_damage.cpp
  src/tooltip.cpp
  src/tooltip_compare.cpp
  src/tooltip_result.cpp
  src/tooltip_sheet.cpp
  src/tooltip_slots.cpp
  src/font.cpp
  src/text.cpp
  src/labels.cpp
  src/components.cpp
  src/requirements.cpp
  src/score.cpp
  src/score_buffs.cpp
  src/score_conversion.cpp
  src/score_damage.cpp
  src/score_freeze.cpp
  src/score_match.cpp
  src/score_other.cpp
  src/score_preset.cpp
  src/score_retaliation.cpp
  src/score_side.cpp
  src/score_skills.cpp
  src/score_states.cpp
  src/score_survival.cpp
  src/sets.cpp
  src/effects.cpp
  src/effects_pets.cpp
  src/utility.cpp
  src/enemy.cpp
  src/classes.cpp
)
target_link_libraries(gdc_core PRIVATE miniz)
target_include_directories(gdc_core PUBLIC include)
target_include_directories(gdc_core SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
if(MSVC)
  target_compile_options(gdc_core PRIVATE /W4)
else()
  target_compile_options(gdc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
