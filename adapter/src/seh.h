#pragma once
// Run a callable under SEH. The frame with __try holds no C++ objects (clang-cl/MSVC rule);
// destructors inside the callable do not run on a fault (leak, never a crash).
#include <windows.h>

#include <cstdint>

namespace gdc::seh {

struct Fault {
  DWORD code = 0;
  std::uintptr_t addr = 0;
};

using Thunk = void (*)(void*);
bool call(Thunk fn, void* arg, Fault& fault);  // false: an exception was caught, see fault

template <class F>
bool run(F& f, Fault& fault) {
  return call([](void* p) { (*static_cast<F*>(p))(); }, &f, fault);
}

}  // namespace gdc::seh
