#include "scripting/PythonHost.h"
#include "scripting/EngineModule.h"

// NOTE: Python.h should be included early.
#include <Python.h>

#include <cstdio>

static PyObject* get_attr_or_null(PyObject* obj, const char* name) {
  if (!obj) return nullptr;
  PyObject* attr = PyObject_GetAttrString(obj, name);
  if (!attr) {
    PyErr_Clear();
    return nullptr;
  }
  return attr;
}

PythonHost::PythonHost() = default;

PythonHost::~PythonHost() {
  shutdown();
}

void PythonHost::set_error_from_python(const char* context) {
  m_lastError.clear();

  if (!PyErr_Occurred()) {
    m_lastError = context;
    return;
  }

  PyObject *ptype = nullptr, *pvalue = nullptr, *ptrace = nullptr;
  PyErr_Fetch(&ptype, &pvalue, &ptrace);
  PyErr_NormalizeException(&ptype, &pvalue, &ptrace);

  PyObject* s = pvalue ? PyObject_Str(pvalue) : nullptr;
  const char* msg = s ? PyUnicode_AsUTF8(s) : nullptr;

  char buf[2048] = {};
  std::snprintf(buf, sizeof(buf), "%s: %s", context, (msg ? msg : "(unknown python error)"));
  m_lastError = buf;

  Py_XDECREF(s);
  Py_XDECREF(ptype);
  Py_XDECREF(pvalue);
  Py_XDECREF(ptrace);
}

bool PythonHost::init(const PythonHostConfig& cfg) {
  shutdown();

  if (!Py_IsInitialized()) {
    // Register built-in modules BEFORE interpreter init.
    if (!EngineModule::Register()) {
      m_lastError = "PyImport_AppendInittab(engine) failed";
      m_ok = false;
      return false;
    }
    Py_Initialize();
    m_pyInitialized = true;
  }

  // sys.path insert
  PyObject* sysPath = PySys_GetObject("path"); // borrowed
  if (!sysPath) {
    set_error_from_python("PySys_GetObject(path)");
    m_ok = false;
    return false;
  }
  PyObject* dir = PyUnicode_FromString(cfg.scriptsDir.c_str());
  if (!dir) {
    set_error_from_python("PyUnicode_FromString(scriptsDir)");
    m_ok = false;
    return false;
  }
  // Insert at front so our scripts override system modules if same name.
  if (PyList_Insert(sysPath, 0, dir) != 0) {
    Py_DECREF(dir);
    set_error_from_python("PyList_Insert(sys.path)");
    m_ok = false;
    return false;
  }
  Py_DECREF(dir);

  PyObject* name = PyUnicode_FromString(cfg.moduleName.c_str());
  if (!name) {
    set_error_from_python("PyUnicode_FromString(moduleName)");
    m_ok = false;
    return false;
  }

  m_module = PyImport_Import(name);
  Py_DECREF(name);

  if (!m_module) {
    set_error_from_python("PyImport_Import(game module)");
    m_ok = false;
    return false;
  }

  m_initFn     = get_attr_or_null(m_module, "init");
  m_updateFn   = get_attr_or_null(m_module, "update");
  m_eventFn    = get_attr_or_null(m_module, "on_event");
  m_shutdownFn = get_attr_or_null(m_module, "shutdown");

  m_ok = true;
  m_lastError.clear();
  return true;
}

void PythonHost::shutdown() {
  if (m_ok) {
    // Give script a chance to cleanup.
    call_event("host", 0, 0);
  }

  Py_XDECREF(m_shutdownFn);
  Py_XDECREF(m_eventFn);
  Py_XDECREF(m_updateFn);
  Py_XDECREF(m_initFn);
  Py_XDECREF(m_module);

  m_shutdownFn = nullptr;
  m_eventFn = nullptr;
  m_updateFn = nullptr;
  m_initFn = nullptr;
  m_module = nullptr;

  m_ok = false;

  if (m_pyInitialized && Py_IsInitialized()) {
    Py_Finalize();
  }
  m_pyInitialized = false;
}

void PythonHost::call_init() {
  if (!m_ok || !m_initFn) return;
  if (!PyCallable_Check(m_initFn)) return;
  PyObject* r = PyObject_CallObject(m_initFn, nullptr);
  if (!r) {
    set_error_from_python("python init()");
    std::printf("[PYERR] %s\n", m_lastError.c_str());
    PyErr_Clear();
    return;
  }
  Py_DECREF(r);
}

void PythonHost::call_update(float dtSeconds) {
  if (!m_ok || !m_updateFn) return;
  if (!PyCallable_Check(m_updateFn)) return;

  PyObject* args = PyTuple_New(1);
  PyTuple_SET_ITEM(args, 0, PyFloat_FromDouble((double)dtSeconds)); // steals ref
  PyObject* r = PyObject_CallObject(m_updateFn, args);
  Py_DECREF(args);

  if (!r) {
    set_error_from_python("python update(dt)");
    std::printf("[PYERR] %s\n", m_lastError.c_str());
    PyErr_Clear();
    return;
  }
  Py_DECREF(r);
}

void PythonHost::call_event(const char* type, int a, int b) {
  if (!m_ok || !m_eventFn) return;
  if (!PyCallable_Check(m_eventFn)) return;

  PyObject* args = PyTuple_New(3);
  PyTuple_SET_ITEM(args, 0, PyUnicode_FromString(type)); // steals ref
  PyTuple_SET_ITEM(args, 1, PyLong_FromLong(a));
  PyTuple_SET_ITEM(args, 2, PyLong_FromLong(b));

  PyObject* r = PyObject_CallObject(m_eventFn, args);
  Py_DECREF(args);
  if (!r) {
    set_error_from_python("python on_event(type,a,b)");
    std::printf("[PYERR] %s\n", m_lastError.c_str());
    PyErr_Clear();
    return;
  }
  Py_DECREF(r);
}
