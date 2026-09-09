// DeepSeekIDE — кастомный AI IDE на C++ / Dear ImGui.
// Точка входа: создаёт приложение и крутит главный цикл.

#include "app/Application.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  return Application().Run(__argc, __argv);
}
#else
int main(int argc, char** argv) {
  return Application().Run(argc, argv);
}
#endif
