#include "app/Platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX 1   // иначе min/max-макросы windows.h ломают std::min/std::max
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace platform {

std::string PathToStr(const std::filesystem::path& p) {
#if defined(_WIN32)
  auto u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
  return p.string();
#endif
}

std::filesystem::path StrToPath(const std::string& utf8) {
#if defined(_WIN32)
  const auto* p = reinterpret_cast<const char8_t*>(utf8.data());
  return std::filesystem::path(std::u8string(p, p + utf8.size()));
#else
  return std::filesystem::path(utf8);
#endif
}

std::filesystem::path HomeDir() {
#ifdef _WIN32
  if (const char* up = std::getenv("USERPROFILE")) return StrToPath(up);
  if (const char* hd = std::getenv("HOMEDRIVE")) {
    if (const char* hp = std::getenv("HOMEPATH")) return StrToPath(std::string(hd) + hp);
  }
  return std::filesystem::current_path();
#else
  if (const char* h = std::getenv("HOME")) return std::filesystem::path(h);
  return std::filesystem::current_path();
#endif
}

std::filesystem::path ConfigDir() {
  std::filesystem::path dir;
#ifdef _WIN32
  if (const char* ad = std::getenv("APPDATA"))
    dir = StrToPath(ad) / "DeepSeekIDE";
  else
    dir = HomeDir() / "AppData" / "Roaming" / "DeepSeekIDE";
#elif defined(__APPLE__)
  dir = HomeDir() / "Library" / "Application Support" / "DeepSeekIDE";
#else
  dir = HomeDir() / ".deepseekide";
#endif
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

long long NowMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

static std::tm LocalTime(std::time_t t) {
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  return tm;
}

std::string NowHMS() {
  auto tm = LocalTime(std::time(nullptr));
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

std::string NowIso() {
  auto tm = LocalTime(std::time(nullptr));
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

std::string FormatMillis(long long epochMs) {
  auto tm = LocalTime(static_cast<std::time_t>(epochMs / 1000));
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%02d.%02d %02d:%02d", tm.tm_mday, tm.tm_mon + 1,
                tm.tm_hour, tm.tm_min);
  return buf;
}

std::string TimeAgo(long long epochMs) {
  long long diff = (NowMillis() - epochMs) / 1000;
  if (diff < 0) diff = 0;
  if (diff < 60) return std::to_string(diff) + " с назад";
  if (diff < 3600) return std::to_string(diff / 60) + " мин назад";
  if (diff < 86400) return std::to_string(diff / 3600) + " ч назад";
  return std::to_string(diff / 86400) + " дн назад";
}

bool ReadTextFile(const std::filesystem::path& p, std::string& out, std::uintmax_t maxBytes) {
  std::error_code ec;
  auto sz = std::filesystem::file_size(p, ec);
  if (ec) return false;
  if (maxBytes > 0 && sz > maxBytes) sz = maxBytes;
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.resize(static_cast<size_t>(sz));
  if (sz > 0) f.read(out.data(), static_cast<std::streamsize>(sz));
  out.resize(static_cast<size_t>(f.gcount()));
  return true;
}

bool WriteTextFile(const std::filesystem::path& p, const std::string& data, std::string* err) {
  std::error_code ec;
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (err) *err = "не удалось открыть файл для записи: " + PathToStr(p);
    return false;
  }
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!f.good()) {
    if (err) *err = "ошибка записи файла: " + PathToStr(p);
    return false;
  }
  return true;
}

std::string HumanSize(std::uintmax_t bytes) {
  const char* units[] = {"Б", "КБ", "МБ", "ГБ"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 3) {
    v /= 1024.0;
    ++u;
  }
  char buf[32];
  if (u == 0)
    std::snprintf(buf, sizeof(buf), "%llu %s", static_cast<unsigned long long>(bytes), units[u]);
  else
    std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
  return buf;
}

#ifndef _WIN32
// POSIX: fork + pipe + poll с таймаутом.
CommandResult RunCommandCapture(const std::string& command, const std::filesystem::path& cwd,
                                int timeoutSec, std::uintmax_t maxOutputBytes) {
  CommandResult res;
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    res.started = false;
    res.output = "pipe() failed";
    return res;
  }
  pid_t pid = fork();
  if (pid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    res.started = false;
    res.output = "fork() failed";
    return res;
  }
  if (pid == 0) {
    // Child
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    std::string dir = PathToStr(cwd);
    if (!dir.empty()) chdir(dir.c_str());
    execl("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
    _exit(127);
  }
  close(pipefd[1]);
  fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

  std::string out;
  out.reserve(4096);
  char buf[4096];
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec > 0 ? timeoutSec : 30);
  bool childDone = false;
  bool killed = false;
  int status = 0;

  while (true) {
    struct pollfd pfd { pipefd[0], POLLIN, 0 };
    int pr = poll(&pfd, 1, 50);
    if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
      ssize_t n = read(pipefd[0], buf, sizeof(buf));
      while (n > 0) {
        if (out.size() < maxOutputBytes) {
          size_t room = static_cast<size_t>(maxOutputBytes - out.size());
          out.append(buf, std::min(room, static_cast<size_t>(n)));
        }
        n = read(pipefd[0], buf, sizeof(buf));
      }
    }
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
      childDone = true;
      // дочитываем остаток
      ssize_t n;
      while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        if (out.size() < maxOutputBytes) {
          size_t room = static_cast<size_t>(maxOutputBytes - out.size());
          out.append(buf, std::min(room, static_cast<size_t>(n)));
        }
      }
      break;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      killed = true;
      childDone = true;
      break;
    }
  }
  close(pipefd[0]);
  (void)childDone;
  res.timedOut = killed;
  if (!killed) {
    if (WIFEXITED(status)) res.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) res.exitCode = 128 + WTERMSIG(status);
  }
  res.output = std::move(out);
  return res;
}
#else
// Windows: _popen + chdir (без жёсткого таймаута — команда выполняется до конца).
CommandResult RunCommandCapture(const std::string& command, const std::filesystem::path& cwd,
                                int /*timeoutSec*/, std::uintmax_t maxOutputBytes) {
  CommandResult res;
  std::string full = "cd /d \"" + PathToStr(cwd) + "\" && " + command + " 2>&1";
  FILE* pipe = _popen(full.c_str(), "r");
  if (!pipe) {
    res.started = false;
    res.output = "не удалось запустить команду";
    return res;
  }
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) {
    if (out.size() < maxOutputBytes) {
      size_t room = static_cast<size_t>(maxOutputBytes - out.size());
      out.append(buf, std::min(room, n));
    }
  }
  res.exitCode = _pclose(pipe);
  res.output = std::move(out);
  return res;
}
#endif

}  // namespace platform
