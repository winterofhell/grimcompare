add_library(gdcompare SHARED
  src/dllmain.cpp
  src/log.cpp
)
set_target_properties(gdcompare PROPERTIES OUTPUT_NAME GDCompare SUFFIX .asi)
target_link_libraries(gdcompare PRIVATE gdc_core bcrypt)
target_compile_definitions(gdcompare PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE PSAPI_VERSION=2)
target_compile_options(gdcompare PRIVATE /W4)
