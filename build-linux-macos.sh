#!/usr/bin/env bash
# ============================================================
#  DeepSeekIDE - сборка в 1 команду для Linux / macOS
#
#  Зависимости:
#    Ubuntu/Debian: sudo apt install build-essential cmake ninja-build \
#                    libglfw3-dev libcurl4-openssl-dev xorg-dev
#                    (+ libwebkit2gtk-4.1-dev для окна chat.deepseek.com)
#    Fedora:        sudo dnf install cmake ninja-build glfw-devel libcurl-devel
#    macOS:         brew install cmake ninja glfw curl
#
#  Опции CMake можно передать аргументами, например:
#    ./build-linux-macos.sh -DDEEPSEEKIDE_ENABLE_WEBVIEW=OFF
# ============================================================
set -e
cd "$(dirname "$0")"

command -v cmake >/dev/null 2>&1 || {
  echo "[ОШИБКА] cmake не найден. Установите: sudo apt install cmake (или brew install cmake)"
  exit 1
}

GEN="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GEN="Ninja"

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "[1/2] Конфигурация ($GEN)..."
echo "      (при первом запуске CMake скачает зависимости с GitHub - это нормально)"
cmake -B build -G "$GEN" -DCMAKE_BUILD_TYPE=Release "$@"

echo "[2/2] Сборка (-j$JOBS)..."
cmake --build build -j"$JOBS"

echo ""
echo "============================================================"
echo "  ГОТОВО! Запуск:  ./build/deepseekide"
echo "  Самотесты ядра:  ./build/deepseekide_selftest"
echo "============================================================"
