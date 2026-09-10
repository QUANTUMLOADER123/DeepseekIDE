// DeepSeekIDE — точка входа (сервер-эпоха).
//
// Приложение = локальный HTTP-сервер (cpp-httplib) + фронтенд на
// HTML/CSS/JS + Monaco Editor, открываемый в обычном браузере пользователя.
// Агент DeepSeek работает через вкладку chat.deepseek.com в браузере
// Chrome/Edge под управлением CDP (ChatDriver) — никакого WebView2.
//
// Флаги:
//   --no-browser : не открывать вкладку автоматически (напечатать ссылку)
//   --port N     : занять указанный порт (иначе автовыбор)

#include <csignal>
#include <cstdio>
#include <exception>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

#include "ai/ToolRegistry.h"
#include "app/Platform.h"
#include "chat/ChatDriver.h"
#include "core/ProjectManager.h"
#include "core/Settings.h"
#include "core/SnapshotManager.h"
#include "server/IdeServer.h"

namespace {

std::ofstream gBoot;
void Boot(const std::string& s) {
  gBoot << s << "\n";
  gBoot.flush();
}

std::string RandomToken() {
  static const char kAlpha[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(0, 61);
  std::string t(28, 'a');
  for (char& c : t) c = kAlpha[dist(gen)];
  return t;
}

std::filesystem::path FindWebRoot() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path cur = fs::current_path(ec);
  for (int i = 0; i < 6; ++i) {
    fs::path cand = cur / "assets" / "web";
    if (fs::is_directory(cand, ec)) return cand;
    cur = cur.parent_path();
    if (cur.empty()) break;
  }
  return fs::current_path(ec) / "assets" / "web";
}

}  // namespace

int Run(int argc, char* argv[]) {
  bool noBrowser = false;
  int forcedPort = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--no-browser")
      noBrowser = true;
    else if (a == "--port" && i + 1 < argc)
      forcedPort = atoi(argv[++i]);
    else if (a.rfind("--port=", 0) == 0)
      forcedPort = atoi(a.c_str() + 7);
  }

  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path cfgDir = platform::ConfigDir();
  fs::create_directories(cfgDir, ec);
#if defined(_WIN32) && defined(_MSC_VER)
  gBoot.open(cfgDir / "boot-server.log", std::ios::out | std::ios::trunc);  // MSVC: native wchar_t пути
#else
  gBoot.open(platform::PathToStr(cfgDir / "boot-server.log"), std::ios::out | std::ios::trunc);
#endif
  Boot("DeepSeekIDE (сервер-эпоха) запуск");

  // Настройки (lastProject и пр.)
  Settings settings = Settings::Load();

  // Ядро: проект, снимки
  ProjectManager project;
  SnapshotManager snaps;

  // Сервер (лог фронту живёт здесь — объявляем до ToolRegistry/ChatDriver)
  IdeServer server;

  // Инструменты агента
  ToolContext toolCtx;
  toolCtx.project = &project;
  toolCtx.snapshots = &snaps;
  toolCtx.log = [&](const std::string& level, const std::string& msg) {
    server.Log(level, msg);
  };
  toolCtx.fileChanged = [&](const std::string& rel) { server.NotifyFileChanged(rel); };
  toolCtx.allowShell = [&] { return settings.allowShell; };
  toolCtx.shellTimeoutSec = [&] { return settings.shellTimeout; };
  ToolRegistry tools(toolCtx);

  // Драйвер чата (CDP → chat.deepseek.com)
  ChatDriver::Cfg chatCfg;
  chatCfg.tools = &tools;
  chatCfg.log = [&](const std::string& level, const std::string& msg) {
    server.Log(level, msg);
    Boot("[" + level + "] " + msg);
  };
  chatCfg.onSession = [&](const std::string& task, const ParsedOps& parsed) {
    server.RecordSession(task, parsed);
  };
  ChatDriver chat(std::move(chatCfg));

  // Открыть проект из последней сессии
  auto doOpenProject = [&](const std::string& path) {
    std::error_code e;
    auto abs = platform::StrToPath(path);
    if (abs.empty() || !fs::is_directory(abs, e)) return false;
    project.SetRoot(abs);
    project.Refresh();
    snaps.SetRoot(abs);
    settings.lastProject = path;
    settings.Save();
    Boot("проект открыт: " + path);
    server.Log("info", "Открыт проект: " + path);
    return true;
  };
  if (!settings.lastProject.empty()) (void)doOpenProject(settings.lastProject);

  // Фронтенд
  fs::path webRoot = FindWebRoot();
  Boot("web root: " + platform::PathToStr(webRoot));

  // Порт и старт
  int port = forcedPort;
  if (port == 0) port = platform::FindFreeTcpPort(27120, 27420);
  if (port == 0) {
    server.Log("error", "Нет свободного порта в диапазоне 27120-27420");
    std::fprintf(stderr, "DeepSeekIDE: нет свободного порта 27120-27420\n");
    return 3;
  }

  IdeServer::Cfg srvCfg;
  srvCfg.tools = &tools;
  srvCfg.project = &project;
  srvCfg.snaps = &snaps;
  srvCfg.chat = &chat;
  srvCfg.webRoot = webRoot;
  srvCfg.token = RandomToken();
  srvCfg.onOpenProject = doOpenProject;

  std::string err;
  if (!server.Start(srvCfg, port, err)) {
    server.Log("error", "Старт сервера: " + err);
    std::fprintf(stderr, "DeepSeekIDE: %s\n", err.c_str());
    return 3;
  }
  const std::string url = server.Url();
  Boot("сервер: " + url);
  server.Log("info", "GUI: " + url);

  // Драйвер чата запускаем после сервера (логи уходят в него)
  chat.Start();

  std::cout << "\n  DeepSeekIDE запущена.\n  Откройте: " << url << "\n"
            << "  Ctrl+C или закрытие терминала — выход.\n";

  if (!noBrowser) {
    if (platform::OpenInBrowser(url))
      Boot("вкладка браузера открыта");
    else
      Boot("не смог открыть браузер — ссылка напечатана в консоль");
  }

#ifndef _WIN32
  // Ждём Ctrl+C / SIGTERM. На Linux/Unix listen-блокирует Start() внутри
  // IdeServer, поэтому просто спим.
  std::signal(SIGINT, [](int) { std::exit(0); });
#endif
#ifdef _WIN32
  // В консоли: Ctrl+C убьёт процесс — деструкторы всё почистят.
#endif

  // Главный цикл: живём, пока сервер живёт (шатдаун команда /api/shutdown).
  while (!server.IsShuttingDown()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  server.Log("info", "Завершаюсь…");
  chat.Stop();    // до сервера: логи драйвера идут в сервер
  server.Stop();
  Boot("выход");
  return 0;
}

int main(int argc, char* argv[]) {
  // Любой необработанный вызов std::terminate (исключение в std::thread и т.п.)
  // раньше давал «приложение упало» без следа. Пишем маркер в boot-server.log,
  // чтобы следующий запуск показал, на какой стадии всё умерло.
  std::set_terminate([] {
    if (gBoot.is_open()) {
      gBoot << "\n[КРАХ] std::terminate: необработанное исключение (см. последнюю строку выше)\n";
      gBoot.flush();
    }
  });
#ifndef _WIN32
  std::signal(SIGSEGV, [](int) {
    if (gBoot.is_open()) { gBoot << "\n[КРАХ] SIGSEGV (обращение к памяти)\n"; gBoot.flush(); }
    _Exit(2);
  });
  std::signal(SIGABRT, [](int) {
    if (gBoot.is_open()) { gBoot << "\n[КРАХ] SIGABRT (assert/terminate)\n"; gBoot.flush(); }
    _Exit(2);
  });
#endif
  try {
    return Run(argc, argv);
  } catch (const std::exception& e) {
    if (gBoot.is_open()) {
      gBoot << "\n[КРАХ] исключение в main: " << e.what() << "\n";
      gBoot.flush();
    }
    std::fprintf(stderr, "DeepSeekIDE КРАХ: %s\n(подробности в %%APPDATA%%/DeepSeekIDE/boot-server.log)\n",
                 e.what());
    return 2;
  } catch (...) {
    if (gBoot.is_open()) { gBoot << "\n[КРАХ] неизвестное исключение\n"; gBoot.flush(); }
    std::fprintf(stderr, "DeepSeekIDE КРАХ: неизвестное исключение\n");
    return 2;
  }
}
