#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace input { struct InputState; }

namespace scripting {

struct EngineContext {
  HWND hwnd = nullptr;
  input::InputState* input = nullptr;
  bool* requestQuit = nullptr;
};

/// Registers the built-in Python module named "engine".
/// Must be called BEFORE Py_Initialize(), via PyImport_AppendInittab.
bool RegisterEngineModule();

/// Provide runtime context (HWND, input, quit flag) used by engine.* functions.
void SetEngineContext(const EngineContext& ctx);

} // namespace scripting

#define PY_SSIZE_T_CLEAN
#include <Python.h>
extern "C" PyMODINIT_FUNC PyInit_engine(void);
