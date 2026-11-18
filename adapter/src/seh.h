#pragma once
// No C++ objects in __try; a fault leaks, never crashes.
#include <windows.h>

#include <cstdint>

namespace gdc::seh {

struct Fault {
  DWORD code = 0;
  std::uintptr_t addr = 0;
};

using Thunk = void (*)(void*);
bool call(Thunk fn, void* arg, Fault& fault);  // false: see fault

template <class F>
bool run(F& f, Fault& fault) {
  return call([](void* p) { (*static_cast<F*>(p))(); }, &f, fault);
}

}  // namespace gdc::seh
