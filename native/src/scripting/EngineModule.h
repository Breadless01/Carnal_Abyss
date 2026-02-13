#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// NOTE: Python.h must be included early in any TU that uses the CPython C-API.
#include <Python.h>

struct EngineContext {
  HWND hwnd = nullptr;
};

namespace EngineModule {
  // Provide the module with access to host-side data (window handle etc.)
  void SetContext(EngineContext* ctx);

  // Register the built-in module "engine" with the embedded interpreter.
  // Must be called BEFORE Py_Initialize().
  bool Register();
}
