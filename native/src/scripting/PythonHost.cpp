#include "PythonHost.h"
#include "EngineModule.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void AddSysPathFront(const char* path) {
  if (!path || !path[0]) return;
  PyObject* sysPath = PySys_GetObject("path"); // borrowed
  if (!sysPath || !PyList_Check(sysPath)) return;
  PyObject* p = PyUnicode_FromString(path);
  if (!p) return;
  PyList_Insert(sysPath, 0, p);
  Py_DECREF(p);
}

#if defined(_WIN32)
static bool IsDir(const std::string& p) {
  DWORD attrs = GetFileAttributesA(p.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return false;
  return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::string DirName(std::string p) {
  for (size_t i = p.size(); i > 0; --i) {
    char c = p[i - 1];
    if (c == '\\' || c == '/') {
      p.resize(i - 1);
      break;
    }
  }
  return p;
}

static void AddProjectPythonCandidatesToSysPath() {
  char exePath[MAX_PATH]{};
  DWORD n = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return;

  std::string exeDir = DirName(std::string(exePath));

  // Typical layouts:
  // - <repo>/build/Release/Game.exe  -> <repo>/python
  // - <repo>/build/Debug/Game.exe    -> <repo>/python
  // - running from repo root         -> <repo>/python already in CWD
  std::vector<std::string> candidates = {
    exeDir + "\\python",
    exeDir + "\\..\\python",
    exeDir + "\\..\\..\\python",
    exeDir + "\\..\\..\\..\\python",
  };

  for (const auto& c : candidates) {
    if (IsDir(c)) {
      AddSysPathFront(c.c_str());
      return; // first match is enough
    }
  }
}
#endif

namespace scripting {

static PyObject* asObj(void* p) { return reinterpret_cast<PyObject*>(p); }

void PythonHost::clearCached() {
  Py_XDECREF(asObj(m_fnUpdate));
  Py_XDECREF(asObj(m_fnOnEvent));
  Py_XDECREF(asObj(m_gameModule));
  m_fnUpdate = nullptr;
  m_fnOnEvent = nullptr;
  m_gameModule = nullptr;
}

bool PythonHost::init(const std::string& gameModuleName) {

  if (m_initialized) return true;

  if (!RegisterEngineModule()) {
    std::printf("[PY] RegisterEngineModule failed\n");
    return false;
  }

  Py_Initialize();
  if (!Py_IsInitialized()) {
    std::printf("[PY] Py_Initialize failed\n");
    return false;
  }

#if defined(_WIN32)
  AddProjectPythonCandidatesToSysPath();
#endif

  m_moduleName = gameModuleName;

  PyObject* name = PyUnicode_FromString(m_moduleName.c_str());
  PyObject* module = PyImport_Import(name);
  Py_DECREF(name);

  if (!module) {
    PyErr_Print();
    PyObject* sysPath = PySys_GetObject("path"); // borrowed
    if (sysPath) {
      PyObject* r = PyObject_Repr(sysPath);
      const char* s = r ? PyUnicode_AsUTF8(r) : nullptr;
      std::printf("[PY] sys.path=%s\n", s ? s : "<unavailable>");
      Py_XDECREF(r);
    }
    std::printf("[PY] Failed to import module '%s'\n", m_moduleName.c_str());
    return false;
  }

  m_gameModule = module;

  // cache optional callables
  PyObject* fnUpdate = PyObject_GetAttrString(module, "update");
  if (fnUpdate && PyCallable_Check(fnUpdate)) {
    m_fnUpdate = fnUpdate;
  } else {
    Py_XDECREF(fnUpdate);
    m_fnUpdate = nullptr;
    std::printf("[PY] Note: no callable update(dt) in %s\n", m_moduleName.c_str());
  }

  PyObject* fnOnEvent = PyObject_GetAttrString(module, "on_event");
  if (fnOnEvent && PyCallable_Check(fnOnEvent)) {
    m_fnOnEvent = fnOnEvent;
  } else {
    Py_XDECREF(fnOnEvent);
    m_fnOnEvent = nullptr;
  }

  m_initialized = true;
  return true;
}

void PythonHost::shutdown() {
  if (!m_initialized) return;

  clearCached();

  Py_Finalize();
  m_initialized = false;
}

void PythonHost::callUpdate(double dtSeconds) {
  if (!m_initialized || !m_fnUpdate) return;

  PyObject* args = Py_BuildValue("(d)", dtSeconds);
  PyObject* res = PyObject_CallObject(asObj(m_fnUpdate), args);
  Py_DECREF(args);

  if (!res) {
    PyErr_Print();
  } else {
    Py_DECREF(res);
  }
}

void PythonHost::callEvent(const char* name, int a, int b, int c) {
  if (!m_initialized || !m_fnOnEvent) return;

  PyObject* args = Py_BuildValue("(siii)", name ? name : "", a, b, c);
  PyObject* res = PyObject_CallObject(asObj(m_fnOnEvent), args);
  Py_DECREF(args);

  if (!res) {
    PyErr_Print();
  } else {
    Py_DECREF(res);
  }
}

} // namespace scripting
