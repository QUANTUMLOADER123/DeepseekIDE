// DeepSeekIDE — кастомный AI IDE на C++ / Dear ImGui.
// Точка входа: создаёт приложение и крутит главный цикл.

#include <exception>
#include <string>

#include "app/Application.h"
#include "app/Platform.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX 1   // min/max-макросы windows.h ломают std::min/std::max
#endif
#include <windows.h>
#endif

namespace {

int GuardRun(int argc, char** argv) {
  try {
    return Application().Run(argc, argv);
  } catch (const std::exception& e) {
    platform::WriteTextFile(
        BootLogPathForMain(),
        std::string("DeepSeekIDE boot log\nFATAL исключение на старте: ") + e.what() + "\n");
    return 2;
  } catch (...) {
    platform::WriteTextFile(BootLogPathForMain(),
                            "DeepSeekIDE boot log\nFATAL неизвестное исключение на старте\n");
    return 2;
  }
}

}  // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  return GuardRun(__argc, __argv);
}
#else
int main(int argc, char** argv) {
  return GuardRun(argc, argv);
}
#endif
