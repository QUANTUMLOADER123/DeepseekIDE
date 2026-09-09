#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace platform {

// Путь к UTF-8 строке (безопасно для русских имён на Windows).
std::string PathToStr(const std::filesystem::path& p);
// UTF-8 строка в путь.
std::filesystem::path StrToPath(const std::string& utf8);

// Домашняя папка пользователя.
std::filesystem::path HomeDir();
// Папка конфигурации приложения (~/.deepseekide или %APPDATA%/DeepSeekIDE),
// создаётся при первом обращении.
std::filesystem::path ConfigDir();

// Текущее время.
long long NowMillis();
std::string NowHMS();                                  // "14:32:07"
std::string NowIso();                                  // "2026-09-09T14:32:07"
std::string FormatMillis(long long epochMs);           // "09.09 14:32"
std::string TimeAgo(long long epochMs);                // "5 мин назад"

// Чтение/запись текстовых файлов (UTF-8).
bool ReadTextFile(const std::filesystem::path& p, std::string& out, std::uintmax_t maxBytes = 0);
bool WriteTextFile(const std::filesystem::path& p, const std::string& data, std::string* err = nullptr);

// Размер файла в человекочитаемом виде.
std::string HumanSize(std::uintmax_t bytes);

// Запуск консольной команды с перехватом stdout/stderr.
struct CommandResult {
  int exitCode = -1;
  std::string output;
  bool timedOut = false;
  bool started = true;
};
CommandResult RunCommandCapture(const std::string& command,
                                const std::filesystem::path& cwd,
                                int timeoutSec,
                                std::uintmax_t maxOutputBytes = 256 * 1024);

}  // namespace platform
