cmake_minimum_required(VERSION 3.24)
project(DeepSeekIDE VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# DeepSeekIDE — кастомный AI-IDE на C++ / Dear ImGui
#  - Агент DeepSeek (function calling) работает с файлами выбранной папки
#  - Снимки состояния и откат изменений
#  - Встроенное окно chat.deepseek.com (WebView2 / WebKitGTK / WKWebView)
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(DEEPSEEKIDE_BUILD_GUI      "Build the Dear ImGui GUI"            ON)
option(DEEPSEEKIDE_BUILD_TESTS    "Build console self-tests"            ON)
option(DEEPSEEKIDE_ENABLE_WEBVIEW "Embedded chat.deepseek.com window"   ON)

include(FetchContent)
find_package(Threads REQUIRED)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# nlohmann_json
# ---------------------------------------------------------------------------
find_package(nlohmann_json 3.11.3 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "[deps] fetching nlohmann_json v3.11.3 (zip-архив, git не нужен)")
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# ---------------------------------------------------------------------------
# libcurl
# ---------------------------------------------------------------------------
find_package(CURL QUIET)
if(NOT CURL_FOUND)
  message(STATUS "[deps] system libcurl not found — building curl 8.10.1 from source")
  set(BUILD_CURL_EXE    OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
  set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
  set(BUILD_MISC_DOCS   OFF CACHE BOOL "" FORCE)
  set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING     OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
  elseif(APPLE)
    set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
  endif()
  FetchContent_Declare(curl
    URL https://github.com/curl/curl/archive/refs/tags/curl-8_10_1.tar.gz)
  FetchContent_MakeAvailable(curl)
endif()

# ---------------------------------------------------------------------------
# Core library: сеть, агент, инструменты, снимки, проект — без GUI-зависимостей
# ---------------------------------------------------------------------------
add_library(deepseekide_core STATIC
  src/app/Platform.cpp
  src/core/Settings.cpp
  src/core/ProjectManager.cpp
  src/core/SnapshotManager.cpp
  src/net/HttpClient.cpp
  src/ai/DeepSeekClient.cpp
  src/ai/ToolRegistry.cpp
  src/ai/Agent.cpp)
target_include_directories(deepseekide_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(deepseekide_core PUBLIC
  nlohmann_json::nlohmann_json
  CURL::libcurl
  Threads::Threads)
if(MSVC)
  target_compile_options(deepseekide_core PRIVATE /W3 /utf-8)
else()
  target_compile_options(deepseekide_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Self-tests (консольные, без GUI)
# ---------------------------------------------------------------------------
if(DEEPSEEKIDE_BUILD_TESTS)
  enable_testing()
  add_executable(deepseekide_selftest src/tests/selftest.cpp)
  target_link_libraries(deepseekide_selftest PRIVATE deepseekide_core)
  add_test(NAME selftest COMMAND deepseekide_selftest)
endif()

# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
if(DEEPSEEKIDE_BUILD_GUI)

  # --- GLFW ---
  find_package(glfw3 3.3 QUIET)
  if(NOT glfw3_FOUND)
    message(STATUS "[deps] fetching GLFW 3.4")
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
      URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz)
    FetchContent_MakeAvailable(glfw)
  endif()

  # --- Dear ImGui (docking branch; CMake у проекта нет — собираем сами) ---
  message(STATUS "[deps] fetching Dear ImGui v1.91.5-docking")
  FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5-docking.tar.gz
    SOURCE_SUBDIR  "__no_cmakelists__")   # пропускаем add_subdirectory
  FetchContent_MakeAvailable(imgui)

  add_library(dearimgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(dearimgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp)
  target_link_libraries(dearimgui PUBLIC glfw ${CMAKE_DL_LIBS})
  target_compile_definitions(dearimgui PUBLIC GLFW_INCLUDE_NONE)

  # --- ImGuiColorTextEdit (santaclose fork, MIT; совместим с ImGui 1.91) ---
  message(STATUS "[deps] fetching ImGuiColorTextEdit (santaclose)")
  FetchContent_Declare(textedit
    URL https://github.com/santaclose/ImGuiColorTextEdit/archive/264bee49ddc3c789b05d928d09c628649458da47.tar.gz
    SOURCE_SUBDIR  "__no_cmakelists__")
  FetchContent_MakeAvailable(textedit)

  add_library(imcolortextedit STATIC
    ${textedit_SOURCE_DIR}/TextEditor.cpp
    ${textedit_SOURCE_DIR}/LanguageDefinitions.cpp)
  target_include_directories(imcolortextedit PUBLIC ${textedit_SOURCE_DIR})
  target_link_libraries(imcolortextedit PUBLIC dearimgui)

  # Вилке нужен boost/regex.hpp: используем настоящий Boost, если он установлен,
  # иначе — наш заголовок-shim на базе std::regex (boost не требуется).
  # (FindBoost удалён из CMake 4.1+, поэтому ищем только если модуль доступен.)
  if(CMAKE_VERSION VERSION_LESS 4.1)
    find_package(Boost QUIET COMPONENTS regex)
  endif()
  if(Boost_FOUND)
    message(STATUS "[deps] ImGuiColorTextEdit: используем системный Boost")
    target_link_libraries(imcolortextedit PRIVATE Boost::regex)
  else()
    message(STATUS "[deps] ImGuiColorTextEdit: используем std::regex shim (boost/regex.hpp)")
    target_include_directories(imcolortextedit PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost_regex_shim)
  endif()

  # --- ImGui, собранный поверх системного GL, линкуем GLFW+dl; OpenGL
  #     функции грузятся встроенным загрузчиком бэкенда ---

  # --- WebView (chat.deepseek.com) ---
  set(DEEPSEEKIDE_WEBVIEW OFF)
  if(DEEPSEEKIDE_ENABLE_WEBVIEW)
    set(_webview_ok OFF)
    if(WIN32 OR APPLE)
      set(_webview_ok ON)
    else()
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(WEBKIT2GTK QUIET IMPORTED_TARGET webkit2gtk-4.1)
        if(NOT WEBKIT2GTK_FOUND)
          pkg_check_modules(WEBKIT2GTK QUIET IMPORTED_TARGET webkit2gtk-4.0)
        endif()
        if(WEBKIT2GTK_FOUND)
          set(_webview_ok ON)
        endif()
      endif()
    endif()
    if(_webview_ok)
      message(STATUS "[deps] fetching webview 0.12.0 (chat.deepseek.com)")
      FetchContent_Declare(webview
        URL https://github.com/webview/webview/archive/refs/tags/0.12.0.tar.gz)
      FetchContent_MakeAvailable(webview)
      set(DEEPSEEKIDE_WEBVIEW ON)
    else()
      message(WARNING "WebKitGTK не найден — окно chat.deepseek.com будет отключено (DEEPSEEKIDE_WEBVIEW=OFF)")
    endif()
  endif()

  # --- Исполняемый файл IDE ---
  set(GUI_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/core/EditorManager.cpp
    src/ui/Theme.cpp
    src/ui/Fonts.cpp
    src/ui/Widgets.cpp
    src/ui/Dialogs.cpp
    src/ui/FileExplorerPanel.cpp
    src/ui/EditorPanel.cpp
    src/ui/ChatPanel.cpp
    src/ui/SnapshotsPanel.cpp
    src/ui/LogPanel.cpp
    src/ui/StatusBar.cpp
    src/ui/WebChatWindow.cpp)

  if(WIN32)
    add_executable(deepseekide WIN32 ${GUI_SOURCES})
  elseif(APPLE)
    add_executable(deepseekide MACOSX_BUNDLE ${GUI_SOURCES})
  else()
    add_executable(deepseekide ${GUI_SOURCES})
  endif()

  target_link_libraries(deepseekide PRIVATE
    deepseekide_core
    dearimgui
    imcolortextedit
    glfw
    Threads::Threads
    ${CMAKE_DL_LIBS})

  if(DEEPSEEKIDE_WEBVIEW)
    target_compile_definitions(deepseekide PRIVATE DEEPSEEKIDE_WEBVIEW=1)
    target_link_libraries(deepseekide PRIVATE webview::core)
  endif()

  if(MSVC)
    target_compile_options(deepseekide PRIVATE /W3 /utf-8)
  else()
    target_compile_options(deepseekide PRIVATE -Wall -Wextra)
  endif()

  # Ассеты (шрифты) кладём рядом с бинарником
  add_custom_command(TARGET deepseekide POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/assets
      $<TARGET_FILE_DIR:deepseekide>/assets)

  install(TARGETS deepseekide RUNTIME DESTINATION bin BUNDLE DESTINATION .)
  install(DIRECTORY assets DESTINATION share/deepseekide)
endif()
