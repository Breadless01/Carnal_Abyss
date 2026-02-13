#include "PythonHost.h"
#include "EngineModule.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdio>

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

  m_moduleName = gameModuleName;

  PyObject* name = PyUnicode_FromString(m_moduleName.c_str());
  PyObject* module = PyImport_Import(name);
  Py_DECREF(name);

  if (!module) {
    PyErr_Print();
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
