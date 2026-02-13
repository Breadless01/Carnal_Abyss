#include "scripting/EngineModule.h"

#include <cstdio>
#include <string>
#include <chrono>

static EngineContext* g_ctx = nullptr;

void EngineModule::SetContext(EngineContext* ctx) {
  g_ctx = ctx;
}

// ----------------------- helpers -----------------------
static PyObject* py_log(PyObject*, PyObject* args) {
  const char* msg = nullptr;
  if (!PyArg_ParseTuple(args, "s", &msg)) return nullptr;
  std::printf("[PY] %s\n", msg ? msg : "");
  Py_RETURN_NONE;
}

static PyObject* py_set_window_title(PyObject*, PyObject* args) {
  const char* title = nullptr;
  if (!PyArg_ParseTuple(args, "s", &title)) return nullptr;

  if (!g_ctx || !g_ctx->hwnd) {
    PyErr_SetString(PyExc_RuntimeError, "engine.set_window_title: host window not ready");
    return nullptr;
  }

  SetWindowTextA(g_ctx->hwnd, title ? title : "");
  Py_RETURN_NONE;
}

static PyObject* py_get_window_size(PyObject*, PyObject* /*args*/) {
  if (!g_ctx || !g_ctx->hwnd) {
    PyErr_SetString(PyExc_RuntimeError, "engine.get_window_size: host window not ready");
    return nullptr;
  }
  RECT r{};
  GetClientRect(g_ctx->hwnd, &r);
  int w = (int)(r.right - r.left);
  int h = (int)(r.bottom - r.top);
  return Py_BuildValue("(ii)", w, h);
}

static PyObject* py_time_seconds(PyObject*, PyObject* /*args*/) {
  // Monotonic-ish clock for simple scripting
  static LARGE_INTEGER freq{};
  static LARGE_INTEGER start{};
  static bool inited = false;
  if (!inited) {
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    inited = true;
  }
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  double t = double(now.QuadPart - start.QuadPart) / double(freq.QuadPart);
  return PyFloat_FromDouble(t);
}

static PyObject* py_request_quit(PyObject*, PyObject* /*args*/) {
  PostQuitMessage(0);
  Py_RETURN_NONE;
}

// ----------------------- module def -----------------------
static PyMethodDef kEngineMethods[] = {
  {"log", py_log, METH_VARARGS, "Log a message via the host."},
  {"set_window_title", py_set_window_title, METH_VARARGS, "Set the host window title."},
  {"get_window_size", py_get_window_size, METH_NOARGS, "Return (width,height) of the client area."},
  {"time_seconds", py_time_seconds, METH_NOARGS, "Return a monotonic time in seconds since host start."},
  {"request_quit", py_request_quit, METH_NOARGS, "Ask the host to quit (posts WM_QUIT)."},
  {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef kEngineModule = {
  PyModuleDef_HEAD_INIT,
  "engine",
  "Carnal Abyss host engine bindings (minimal).",
  -1,
  kEngineMethods
};

PyMODINIT_FUNC PyInit_engine(void) {
  return PyModule_Create(&kEngineModule);
}

bool EngineModule::Register() {
  // Add a built-in module named "engine". Call before Py_Initialize().
  // If it was already added, CPython returns 0 as success anyway.
  if (PyImport_AppendInittab("engine", &PyInit_engine) != 0) {
    return false;
  }
  return true;
}
