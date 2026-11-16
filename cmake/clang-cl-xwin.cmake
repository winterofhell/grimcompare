# Cross toolchain: Windows x64 DLL from Linux with clang-cl + lld-link + xwin CRT/SDK.
# Expects CLANG_CL and XWIN_DIR in the environment (shell.nix sets both).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED ENV{CLANG_CL} OR NOT DEFINED ENV{XWIN_DIR})
  message(FATAL_ERROR "CLANG_CL and XWIN_DIR must be set (run inside nix-shell)")
endif()
set(XWIN "$ENV{XWIN_DIR}")
if(NOT EXISTS "${XWIN}/crt/include" OR NOT EXISTS "${XWIN}/sdk/include/um")
  message(FATAL_ERROR "xwin splat not found in ${XWIN} (see docs/BUILD.md)")
endif()

set(CMAKE_C_COMPILER "$ENV{CLANG_CL}")
set(CMAKE_CXX_COMPILER "$ENV{CLANG_CL}")
set(CMAKE_LINKER lld-link)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set(_inc "/imsvc${XWIN}/crt/include /imsvc${XWIN}/sdk/include/ucrt /imsvc${XWIN}/sdk/include/um /imsvc${XWIN}/sdk/include/shared")
set(CMAKE_C_FLAGS_INIT "${_inc}")
set(CMAKE_CXX_FLAGS_INIT "${_inc}")

set(_lib "/libpath:${XWIN}/crt/lib/x86_64 /libpath:${XWIN}/sdk/lib/um/x86_64 /libpath:${XWIN}/sdk/lib/ucrt/x86_64 /manifest:no")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_lib}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_lib}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_lib}")

# Dynamic release CRT (/MD) in every config, try_compile included: Game.dll and Engine.dll
# import MSVCP140/VCRUNTIME140/ucrt (winedump, docs/RECON.md §7), so we share their heap.
# The debug CRT (msvcrtd/msvcp140d) is neither in xwin's splat nor in the Proton prefix.
# gdcompare-setup sets MultiThreaded (/MT) instead: a standalone exe that needs no VC++ redistributable.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
