cmake_minimum_required(VERSION 3.20)
project(DeepSeekCppIDE LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
# QTextCodec (кодировка IBM866 для вывода консоли Windows) живёт в qt5compat
find_package(Qt6 QUIET COMPONENTS Core5Compat)
if (Qt6Core5Compat_FOUND)
    add_compile_definitions(HAVE_CORE5COMPAT)
else()
    message(STATUS "qt5compat не установлен — вывод консоли будет в UTF-8/CP1251")
endif()

# --- встроенный wasm3 (интерпретатор WebAssembly — нужен для PoW челленджа)
file(GLOB WASM3_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/wasm3/m3_*.c")
add_library(wasm3 STATIC ${WASM3_SOURCES})
target_include_directories(wasm3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/wasm3")
if (MSVC)
    target_compile_options(wasm3 PRIVATE /W0)
    target_compile_options(wasm3 PRIVATE /wd4244 /wd4267 /wd4996)
else()
    target_compile_options(wasm3 PRIVATE -w)
endif()

set(APP_SOURCES
    src/main.cpp
    src/mainwindow.cpp        src/mainwindow.h
    src/settings.cpp          src/settings.h
    src/pow.cpp               src/pow.h
    src/deepseekclient.cpp    src/deepseekclient.h
    src/agent.cpp             src/agent.h
    src/sessionstore.cpp      src/sessionstore.h
    src/sitepreview.cpp       src/sitepreview.h
    src/squirclecard.h
)


add_executable(DeepSeekCppIDE WIN32 ${APP_SOURCES})
target_include_directories(DeepSeekCppIDE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/wasm3")
target_link_libraries(DeepSeekCppIDE PRIVATE Qt6::Widgets Qt6::Network wasm3 dwmapi)

# ассеты к exe (лого и пр.)
add_custom_command(TARGET DeepSeekCppIDE POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:DeepSeekCppIDE>/assets")
if (Qt6Core5Compat_FOUND)
    target_link_libraries(DeepSeekCppIDE PRIVATE Qt6::Core5Compat)
endif()

if (WIN32 AND MSVC)
    target_compile_options(DeepSeekCppIDE PRIVATE /utf-8)
endif()
