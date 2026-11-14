# Post-build check of GDCompare.asi (adapter/CMakeLists.txt): fail if it imports MSVCP140's mutex or
# condition-variable runtime. The Proton prefix ships a 2016 msvcp140.dll; std::mutex from current MSVC
# headers faults there on first lock (adapter 0.4.0, docs/BUILD.md). Use SRWLOCK or a spin lock.
# usage: cmake -DREADOBJ=<llvm-readobj> -DFILE=<asi> -P check_imports.cmake
execute_process(COMMAND "${READOBJ}" --coff-imports "${FILE}" OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "llvm-readobj failed on ${FILE}")
endif()
string(REGEX MATCHALL "Symbol: _(Mtx|Cnd)_[A-Za-z_]+" bad "${out}")
if(bad)
  message(FATAL_ERROR "${FILE} imports ${bad}: std::mutex / std::condition_variable crash on the prefix's msvcp140.dll")
endif()
