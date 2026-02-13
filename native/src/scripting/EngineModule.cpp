#include "EngineModule.h"
#include "../input/InputState.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdio>
#include <cstring>

namespace scripting {

static EngineContext g_ctx{};

void SetEngineContext(const EngineContext& ctx) { g_ctx = ctx; }

// ------------------- helpers -------------------
static PyObject* py_log(PyObject*, PyObject* args) {
  const char* msg = nullptr;
  if (!PyArg_ParseTuple(args, "s", &msg)) return nullptr;
  std::printf("[PY] %s\n", msg ? msg : "");
  Py_RETURN_NONE;
}

static PyObject* py_set_window_title(PyObject*, PyObject* args) {
  const char* title = nullptr;
  if (!PyArg_ParseTuple(args, "s", &title)) return nullptr;
  if (g_ctx.hwnd && title) {
    SetWindowTextA(g_ctx.hwnd, title);
  }
  Py_RETURN_NONE;
}

static PyObject* py_get_window_size(PyObject*, PyObject*) {
  if (!g_ctx.hwnd) return Py_BuildValue("(ii)", 0, 0);
  RECT r{};
  GetClientRect(g_ctx.hwnd, &r);
  int w = (r.right - r.left);
  int h = (r.bottom - r.top);
  return Py_BuildValue("(ii)", w, h);
}

static PyObject* py_time_seconds(PyObject*, PyObject*) {
  // Python can call time.time() too; this is just convenient + stable.
  LARGE_INTEGER freq{}, now{};
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&now);
  double sec = (freq.QuadPart > 0) ? (double)now.QuadPart / (double)freq.QuadPart : 0.0;
  return PyFloat_FromDouble(sec);
}

static PyObject* py_request_quit(PyObject*, PyObject*) {
  if (g_ctx.requestQuit) *g_ctx.requestQuit = true;
  Py_RETURN_NONE;
}

// --------- input ----------
static PyObject* py_is_key_down(PyObject*, PyObject* args) {
  int vk = 0;
  if (!PyArg_ParseTuple(args, "i", &vk)) return nullptr;
  if (!g_ctx.input) Py_RETURN_FALSE;
  bool down = g_ctx.input->isKeyDown(vk);
  if (down) Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

static PyObject* py_mouse_pos(PyObject*, PyObject*) {
  if (!g_ctx.input) return Py_BuildValue("(ii)", 0, 0);
  return Py_BuildValue("(ii)", g_ctx.input->mouseX, g_ctx.input->mouseY);
}

static PyObject* py_mouse_delta(PyObject*, PyObject*) {
  if (!g_ctx.input) return Py_BuildValue("(ii)", 0, 0);
  return Py_BuildValue("(ii)", g_ctx.input->mouseDX, g_ctx.input->mouseDY);
}

static PyObject* py_mouse_button_down(PyObject*, PyObject* args) {
  int button = 0; // 0=L,1=R,2=M
  if (!PyArg_ParseTuple(args, "i", &button)) return nullptr;
  if (!g_ctx.input) Py_RETURN_FALSE;
  bool down = g_ctx.input->isMouseDown(button);
  if (down) Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

static PyMethodDef kMethods[] = {
  {"log", py_log, METH_VARARGS, "engine.log(str) -> None"},
  {"set_window_title", py_set_window_title, METH_VARARGS, "engine.set_window_title(str) -> None"},
  {"get_window_size", py_get_window_size, METH_NOARGS, "engine.get_window_size() -> (w,h)"},
  {"time_seconds", py_time_seconds, METH_NOARGS, "engine.time_seconds() -> float"},
  {"request_quit", py_request_quit, METH_NOARGS, "engine.request_quit() -> None"},

  {"is_key_down", py_is_key_down, METH_VARARGS, "engine.is_key_down(vk:int) -> bool"},
  {"mouse_pos", py_mouse_pos, METH_NOARGS, "engine.mouse_pos() -> (x,y)"},
  {"mouse_delta", py_mouse_delta, METH_NOARGS, "engine.mouse_delta() -> (dx,dy)"},
  {"mouse_button_down", py_mouse_button_down, METH_VARARGS, "engine.mouse_button_down(btn:int) -> bool"},
  {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef kModule = {
  PyModuleDef_HEAD_INIT,
  "engine",
  "Minimal built-in engine API (embedded).",
  -1,
  kMethods
};

extern "C" PyMODINIT_FUNC PyInit_engine(void) {
  return PyModule_Create(&kModule);
}

bool RegisterEngineModule() {
  // Must happen before Py_Initialize()
  return PyImport_AppendInittab("engine", &PyInit_engine) == 0;
}

} // namespace scripting
