#pragma once

#include <string>

// Minimal embedded-CPython host.
// Loads a python module (default: game.py) and calls optional functions:
//   - init()
//   - update(dt: float)
//   - on_event(type: str, a: int, b: int)
//   - shutdown()
//
// Keep it tiny: you can expand this later into a full gameplay API.

struct PythonHostConfig {
  std::string scriptsDir;     // directory added to sys.path
  std::string moduleName;     // e.g. "game"
};

class PythonHost {
public:
  PythonHost();
  ~PythonHost();

  bool init(const PythonHostConfig& cfg);
  void shutdown();

  void call_init();
  void call_update(float dtSeconds);
  void call_event(const char* type, int a, int b);

  bool ok() const { return m_ok; }
  const std::string& last_error() const { return m_lastError; }

private:
  void set_error_from_python(const char* context);

  bool m_ok = false;
  bool m_pyInitialized = false;
  std::string m_lastError;

  struct _object* m_module = nullptr;   // PyObject*
  struct _object* m_initFn = nullptr;   // PyObject*
  struct _object* m_updateFn = nullptr; // PyObject*
  struct _object* m_eventFn = nullptr;  // PyObject*
  struct _object* m_shutdownFn = nullptr; // PyObject*
};
