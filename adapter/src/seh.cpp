#include "seh.h"

namespace gdc::seh {
namespace {

int capture(EXCEPTION_POINTERS* ep, Fault& f) {
  f.code = ep->ExceptionRecord->ExceptionCode;
  f.addr = reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

bool call(Thunk fn, void* arg, Fault& fault) {
  __try {
    fn(arg);
    return true;
  } __except (capture(GetExceptionInformation(), fault)) {
    return false;
  }
}

}  // namespace gdc::seh
