# gdcompare-setup: installer + build-preset wizard, one console program for Windows and Linux.
add_library(gdc_setup STATIC
  src/buildpreset.cpp
  src/catalog.cpp
  src/detect.cpp
  src/install.cpp
  src/platform.cpp
  src/saves.cpp
  src/sha256.cpp
  src/steam.cpp
  src/tui.cpp
  src/wizard.cpp
)
target_link_libraries(gdc_setup PUBLIC gdc_core)
target_include_directories(gdc_setup PUBLIC src)
target_include_directories(gdc_setup SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party)
if(WIN32)
  target_link_libraries(gdc_setup PUBLIC shell32 ole32 advapi32)
endif()

add_executable(gdcompare-setup src/main.cpp)
target_link_libraries(gdcompare-setup PRIVATE gdc_setup)
if(GDC_SETUP_STATIC AND NOT WIN32)
  # A release binary that runs on any distro. NixOS keeps libc.a apart (shell.nix GLIBC_STATIC_LIB).
  if(DEFINED ENV{GLIBC_STATIC_LIB})
    target_link_directories(gdcompare-setup PRIVATE $ENV{GLIBC_STATIC_LIB})
  endif()
  target_link_options(gdcompare-setup PRIVATE -static -s)
endif()
foreach(t gdc_setup gdcompare-setup)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
