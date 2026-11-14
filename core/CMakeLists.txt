add_library(gdc_core STATIC
  src/pe_exports.cpp
  src/hook_targets.cpp
  src/known_builds.cpp
)
target_include_directories(gdc_core PUBLIC include)
if(MSVC)
  target_compile_options(gdc_core PRIVATE /W4)
else()
  target_compile_options(gdc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
