#pragma once
#include <cstdint>

namespace input {

struct InputState {
  bool keys[256]{};
  bool mouseButtons[3]{};
  int mouseX = 0;
  int mouseY = 0;
  int mouseDX = 0;
  int mouseDY = 0;

  void beginFrame() {
    mouseDX = 0;
    mouseDY = 0;
  }

  void setKeyDown(int vk, bool down) {
    if (vk >= 0 && vk < 256) keys[vk] = down;
  }

  bool isKeyDown(int vk) const {
    if (vk >= 0 && vk < 256) return keys[vk];
    return false;
  }

  void setMouseButtonDown(int button, bool down) {
    if (button >= 0 && button < 3) mouseButtons[button] = down;
  }

  bool isMouseDown(int button) const {
    if (button >= 0 && button < 3) return mouseButtons[button];
    return false;
  }

  void setMousePos(int x, int y) {
    mouseDX += (x - mouseX);
    mouseDY += (y - mouseY);
    mouseX = x;
    mouseY = y;
  }
};

} // namespace input
