#pragma once
#include <string>

namespace scripting {

class PythonHost {
public:
  bool init(const std::string& gameModuleName); // e.g. "game"
  void shutdown();

  void callUpdate(double dtSeconds);
  void callEvent(const char* name, int a=0, int b=0, int c=0);

private:
  bool m_initialized = false;
  std::string m_moduleName;

  // Cached callables
  void* m_gameModule = nullptr;  // PyObject*
  void* m_fnUpdate = nullptr;    // PyObject*
  void* m_fnOnEvent = nullptr;   // PyObject*

  void clearCached();
};

} // namespace scripting
