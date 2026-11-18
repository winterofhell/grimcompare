enable_language(C)
# MinHook 1.3.4 (third_party/minhook, upstream TsudaKageyu/minhook tag v1.3.4, commit c3fcafdc)
add_library(minhook STATIC
  ${PROJECT_SOURCE_DIR}/third_party/minhook/src/buffer.c
  ${PROJECT_SOURCE_DIR}/third_party/minhook/src/hook.c
  ${PROJECT_SOURCE_DIR}/third_party/minhook/src/trampoline.c
  ${PROJECT_SOURCE_DIR}/third_party/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${PROJECT_SOURCE_DIR}/third_party/minhook/include)

add_library(gdcompare SHARED
  src/dllmain.cpp
  src/calibration.cpp
  src/log.cpp
  src/seh.cpp
  src/snapshot.cpp
  src/snapshot_accumulators.cpp
  src/snapshot_character.cpp
  src/snapshot_init.cpp
  src/snapshot_items.cpp
  src/snapshot_pets.cpp
  src/snapshot_skills.cpp
  src/hooks.cpp
  src/tooltip.cpp
  src/tooltip_build.cpp
  src/tooltip_presets.cpp
  src/tooltip_worker.cpp
)
set_target_properties(gdcompare PROPERTIES OUTPUT_NAME GDCompare SUFFIX .asi)
target_link_libraries(gdcompare PRIVATE gdc_core minhook bcrypt)
target_compile_definitions(gdcompare PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE PSAPI_VERSION=2)
target_compile_options(gdcompare PRIVATE /W4)
find_program(LLVM_READOBJ llvm-readobj REQUIRED)
add_custom_command(TARGET gdcompare POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DREADOBJ=${LLVM_READOBJ} -DFILE=$<TARGET_FILE:gdcompare>
          -P ${PROJECT_SOURCE_DIR}/cmake/check_imports.cmake
  VERBATIM)
