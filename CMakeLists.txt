cmake_minimum_required(VERSION 3.20)
project(deepseekide LANGUAGES CXX)

# DeepSeekIDE (сервер-эпоха):
#   deepseekide.exe = локальный HTTP-сервер (cpp-httplib, vendored) +
#                     CDP-драйвер браузера (chat.deepseek.com) +
#                     ядро (проект/снапшоты/инструменты агента, 74+ тестов).
# Фронтенд = assets/web (HTML/CSS/JS + Monaco Editor) — открывается в вашем браузере.
# Внешних зависимостей НЕТ (ни vcpkg, ни curl): HTTPS не нужен, всё по 127.0.0.1.

enable_testing()
option(DSIDE_WARNINGS_AS_ERRORS "Трактовать предупреждения как ошибки" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W3 /EHsc /bigobj /D_CRT_SECURE_NO_WARNINGS
                      /D_UNICODE /DUNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra -Wshadow -fno-omit-frame-pointer)
endif()

# ----------------------------------------------------------------------
# Сторонние вендорнутые зависимости (в репозитории)
# ----------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz)
FetchContent_MakeAvailable(nlohmann_json)

# ----------------------------------------------------------------------
# Ядро + сеть + инструменты
# ----------------------------------------------------------------------
set(SOURCES
  src/core/ProjectManager.cpp
  src/core/SnapshotManager.cpp
  src/core/Settings.cpp
  src/ai/OpsParser.cpp
  src/ai/ToolRegistry.cpp
  src/net/HttpClient.cpp
  src/util/Utf8.cpp
  src/util/TextStitch.cpp
  src/app/Platform.cpp
  src/server/OpsApply.cpp)

add_executable(deepseekide
  ${SOURCES}
  src/ws/WsClient.cpp
  src/cdp/CdpClient.cpp
  src/chat/ChatDriver.cpp
  src/server/IdeServer.cpp
  src/server/OpsApply.cpp
  src/main.cpp)

target_include_directories(deepseekide PRIVATE
  src
  third_party)

target_link_libraries(deepseekide PRIVATE nlohmann_json::nlohmann_json)

# ----------------------------------------------------------------------
# Юнит-тесты отдельным бинарём: deepseekide_tests
# ----------------------------------------------------------------------
add_executable(deepseekide_tests ${SOURCES} src/tests/selftest.cpp)
target_include_directories(deepseekide_tests PRIVATE src third_party)
target_link_libraries(deepseekide_tests PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  target_compile_definitions(deepseekide_tests PRIVATE _WIN32_WINNT=0x0602)
  target_link_libraries(deepseekide_tests PRIVATE ws2_32 shlwapi ole32)
else()
  target_link_libraries(deepseekide_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
add_test(NAME selftest COMMAND deepseekide_tests)

if(WIN32)
  target_compile_definitions(deepseekide PRIVATE _WIN32_WINNT=0x0602)
  target_link_libraries(deepseekide PRIVATE ws2_32 shlwapi ole32)
  # Кладём exe в корень проекта — рядом с assets/web виден "из коробки"
  set_target_properties(deepseekide PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}")
else()
  find_package(Threads REQUIRED)
  target_link_libraries(deepseekide PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
