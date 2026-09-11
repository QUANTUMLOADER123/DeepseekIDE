#include "app/Platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX 1   // иначе min/max-макросы windows.h ломают std::min/std::max
#endif
#include <winsock2.h>   // ДО windows.h: socket/bind для FindFreeTcpPort
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>   // ShellExecuteW
#include <shlobj.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
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

bool AppendTextFile(const std::filesystem::path& p, const std::string& data) {
  std::string old;
  ReadTextFile(p, old);
  return WriteTextFile(p, old + data);
}

// ===================== Браузер для CDP-автоматизации =====================

bool FindChromiumBrowser(std::string& exeOut, std::string& errOut) {
#if defined(_WIN32)
  // 1) Реестр: App Paths
  const wchar_t* keys[] = {
      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe",
      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe",
      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chromium.exe"};
  for (const wchar_t* sub : keys) {
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
      wchar_t buf[1024];
      DWORD sz = sizeof(buf);
      LSTATUS st = RegGetValueW(root, sub, L"", RRF_RT_REG_SZ, nullptr, buf, &sz);
      if (st == ERROR_SUCCESS && buf[0] != L'\0') {
        std::filesystem::path p(buf);
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
          exeOut = PathToStr(p);
          return true;
        }
      }
    }
  }
  // 2) Типовые пути
  const char* candidates[] = {
      "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
      "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
      "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
      "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe"};
  for (const char* path : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      exeOut = path;
      return true;
    }
  }
  errOut = "Не найден Google Chrome или Microsoft Edge. Установите любой из них "
           "(обычный, не Portable) — chat.deepseek.com мы открываем в нём.";
  return false;
#else
  const char* candidates[] = {"/usr/bin/google-chrome", "/usr/bin/chromium",
                              "/usr/bin/chromium-browser", "/usr/bin/microsoft-edge",
                              "/opt/google/chrome/google-chrome"};
  for (const char* path : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      exeOut = path;
      return true;
    }
  }
  errOut = "Chrome/Chromium/Edge не найден в PATH-системных папках.";
  return false;
#endif
}

bool LaunchDetached(const std::string& exe, const std::string& args, std::string& errOut) {
#if defined(_WIN32)
  std::string cmdline = "\"" + exe + "\" " + args;
  std::wstring wCmd(cmdline.begin(), cmdline.end());
  std::wstring wExe(exe.begin(), exe.end());
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // Командная строка должна быть изменяемой — копируем в вектор
  std::vector<wchar_t> cmdBuf(wCmd.begin(), wCmd.end());
  cmdBuf.push_back(L'\0');
  BOOL ok = CreateProcessW(wExe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                           CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr, &si, &pi);
  if (!ok) {
    errOut = "CreateProcessW failed: " + std::to_string(GetLastError());
    return false;
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return true;
#else
  std::string cmd = "\"" + exe + "\" " + args + " >/dev/null 2>&1 &";
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    errOut = "system(launch) rc=" + std::to_string(rc);
    return false;
  }
  return true;
#endif
}

int FindFreeTcpPort(int from, int to) {
  for (int port = from; port <= to; ++port) {
#if defined(_WIN32)
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) break;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(static_cast<u_short>(port));
    BOOL ok = bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0;
    closesocket(s);
    if (ok) return port;
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) break;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(static_cast<uint16_t>(port));
    bool ok = bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0;
    ::close(s);
    if (ok) return port;
#endif
  }
  return 0;
}

bool OpenInBrowser(const std::string& url) {
#if defined(_WIN32)
  std::wstring wUrl(url.begin(), url.end());
  auto r = ShellExecuteW(nullptr, L"open", wUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<intptr_t>(r) > 32;
#elif defined(__APPLE__)
  return std::system(("open \"" + url + "\" &").c_str()) == 0;
#else
  return std::system(("xdg-open \"" + url + "\" >/dev/null 2>&1 &").c_str()) == 0;
#endif
}

}  // namespace platform
