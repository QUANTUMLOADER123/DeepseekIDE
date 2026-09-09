#include "server/IdeServer.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include "ai/ToolRegistry.h"
#include "app/Platform.h"
#include "cpp-httplib/httplib.h"

namespace {

long long NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

nlohmann::json NodeJson(const ProjectNode& n, int depth = 0) {
  nlohmann::json j = {{"name", n.name}, {"path", n.relPath},
                      {"dir", n.isDir},  {"size", n.size}};
  if (n.isDir && depth < 8 && !n.children.empty()) {
    nlohmann::json kids = nlohmann::json::array();
    for (const auto& c : n.children) kids.push_back(NodeJson(c, depth + 1));
    j["children"] = std::move(kids);
  }
  return j;
}

std::string MimeFor(const std::string& name) {
  auto ends = [&](const char* suf) {
    return name.size() >= strlen(suf) &&
           name.compare(name.size() - strlen(suf), std::string::npos, suf) == 0;
  };
  if (ends(".html")) return "text/html; charset=utf-8";
  if (ends(".js") || ends(".mjs")) return "text/javascript; charset=utf-8";
  if (ends(".css")) return "text/css; charset=utf-8";
  if (ends(".json")) return "application/json; charset=utf-8";
  if (ends(".map")) return "application/json";
  if (ends(".svg")) return "image/svg+xml";
  if (ends(".png")) return "image/png";
  if (ends(".ico")) return "image/x-icon";
  if (ends(".ttf")) return "font/ttf";
  if (ends(".woff")) return "font/woff";
  if (ends(".woff2")) return "font/woff2";
  return "application/octet-stream";
}

}  // namespace

// ---------------------------------------------------------------------------

struct IdeServer::Impl {
  httplib::Server srv;
  std::thread th;
  std::atomic<bool> shuttingDown{false};
};

IdeServer::IdeServer() = default;

IdeServer::~IdeServer() { Stop(); }

void IdeServer::Log(const std::string& level, const std::string& msg) {
  std::lock_guard<std::mutex> lk(mLogMtx);
  mLog.push_back(LogEntry{NowMs(), level, msg});
  ++mLogSeq;
  while (mLog.size() > 400) mLog.pop_front();
}

void IdeServer::NotifyFileChanged(const std::string& rel) {
  std::lock_guard<std::mutex> lk(mEvMtx);
  mEvents.push_back({{"type", "fileChanged"}, {"path", rel}});
  ++mEvSeq;
  while (mEvents.size() > 200) mEvents.pop_front();
}

bool IdeServer::IsShuttingDown() const {
  return mImpl && mImpl->shuttingDown.load();
}

std::string IdeServer::Url() const {
  return "http://127.0.0.1:" + std::to_string(mPort) + "/?t=" + mCfg.token;
}

// ---------------------------------------------------------------------------

bool IdeServer::Start(const Cfg& cfg, int port, std::string& errOut) {
  mCfg = cfg;
  mPort = port;
  mImpl = std::make_unique<Impl>();
  auto& srv = mImpl->srv;

  Log("info", "DeepSeekIDE сервер: порт " + std::to_string(port));

  // ---- статика ----
  srv.Get("/", [this](const httplib::Request&, httplib::Response& res) {
    std::string type;
    bool ok = false;
    std::string body = ServeFile("index.html", type, ok);
    if (!ok) {
      res.status = 500;
      res.set_content(
          "<html><body style='background:#0b0e17;color:#e6ebf3;font-family:sans-serif;"
          "padding:40px'><h2>DeepSeekIDE: фронтенд не найден</h2><p>Положите папку "
          "<code>assets/web</code> рядом с deepseekide.exe и перезапустите.</p></body></html>",
          "text/html; charset=utf-8");
      return;
    }
    res.set_content(body, type.c_str());
  });

  srv.Get(R"(/assets/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
    std::string type;
    bool ok = false;
    std::string body = ServeFile(req.matches[1], type, ok);
    if (!ok) {
      res.status = 404;
      res.set_content("not found", "text/plain");
      return;
    }
    res.set_content(body, type.c_str());
  });

  srv.Get(R"(/monaco/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
    std::string type;
    bool ok = false;
    std::string body = ServeFile("monaco/" + std::string(req.matches[1]), type, ok);
    if (!ok) {
      res.status = 404;
      return;
    }
    res.set_header("Cache-Control", "public, max-age=86400");
    res.set_content(body, type.c_str());
  });

  srv.Get("/favicon.ico", [](const httplib::Request&, httplib::Response& res) {
    // Маленький синий ромб (svg)
    res.set_content(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
        "<path d='M16 2 L30 16 L16 30 L2 16 Z' fill='#4D6BFE'/></svg>",
        "image/svg+xml");
  });

  // ---- API (токен) ----
  auto api = [this](const httplib::Request& req, httplib::Response& res,
                    const std::function<nlohmann::json(const nlohmann::json&)>& fn) {
    static_cast<void>(this);
    std::string token = req.get_header_value("X-DeepSeekIDE-Token");
    if (token.empty()) token = req.get_param_value("t");
    if (token != mCfg.token) {
      res.status = 401;
      res.set_content("{\"error\":\"unauthorized\"}", "application/json");
      return;
    }
    nlohmann::json body = nlohmann::json::object();
    if (!req.body.empty()) {
      body = nlohmann::json::parse(req.body, nullptr, false);
      if (body.is_discarded()) {
        res.status = 400;
        res.set_content("{\"error\":\"bad json\"}", "application/json");
        return;
      }
    }
    try {
      nlohmann::json out = fn(body);
      res.set_content(out.dump(), "application/json; charset=utf-8");
    } catch (const std::exception& e) {
      res.status = 500;
      nlohmann::json err = {{"error", e.what()}};
      res.set_content(err.dump(), "application/json");
    }
  };

  // --- состояние ---
  srv.Get("/api/state", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) { return StateJson(); });
  });

  // --- журнал ---
  srv.Get("/api/log", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) {
      std::lock_guard<std::mutex> lk(mLogMtx);
      nlohmann::json entries = nlohmann::json::array();
      for (const auto& e : mLog)
        entries.push_back({{"at", e.atMs}, {"level", e.level}, {"msg", e.msg}});
      return nlohmann::json{{"entries", entries}};
    });
  });

  // --- события файлов ---
  srv.Get("/api/events", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) {
      std::lock_guard<std::mutex> lk(mEvMtx);
      nlohmann::json ev = nlohmann::json::array();
      for (auto& e : mEvents) ev.push_back(e);
      return nlohmann::json{{"events", ev}};
    });
  });

  // --- файл ---
  srv.Get("/api/file", [this, api](const httplib::Request& req, httplib::Response& res) {
    const std::string path = req.get_param_value("path");
    api(req, res, [this, path](const nlohmann::json&) {
      if (!mCfg.project->IsOpen()) return nlohmann::json{{"error", "проект не открыт"}};
      std::filesystem::path abs;
      std::string err;
      if (!ProjectManager::SafeJoin(mCfg.project->Root(), path, abs, err))
        return nlohmann::json{{"error", err}};
      std::error_code ec;
      if (!std::filesystem::is_regular_file(abs, ec))
        return nlohmann::json{{"error", "это не файл"}};
      std::string text;
      if (!platform::ReadTextFile(abs, text, 2 * 1024 * 1024))
        return nlohmann::json{{"error", "не читается (бинарный?)"}};
      return nlohmann::json{{"path", path}, {"content", text}};
    });
  });

  srv.Post("/api/file/save", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json& body) {
      std::string rel = body.value("path", "");
      std::string content = body.value("content", "");
      std::filesystem::path abs;
      std::string err;
      if (!ProjectManager::SafeJoin(mCfg.project->Root(), rel, abs, err))
        return nlohmann::json{{"error", err}};
      if (!platform::WriteTextFile(abs, content, &err))
        return nlohmann::json{{"error", err}};
      return nlohmann::json{{"ok", true}};
    });
  });

  // --- открытие проекта ---
  srv.Post("/api/project/open", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json& body) {
      std::string path = body.value("path", "");
      if (path.empty()) return nlohmann::json{{"error", "path пуст"}};
      std::error_code ec;
      auto p = platform::StrToPath(path);
      if (!std::filesystem::is_directory(p, ec))
        return nlohmann::json{{"error", "папка не найдена: " + path}};
      if (mCfg.onOpenProject) mCfg.onOpenProject(path);
      Log("info", "Открыт проект: " + path);
      return nlohmann::json{{"ok", true}};
    });
  });

  // --- снимки ---
  srv.Get("/api/snapshots", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) { return SnapshotsJson(); });
  });

  srv.Post("/api/snapshots/rollback", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json& body) {
      std::string id = body.value("id", "");
      std::string lg;
      bool ok = id.empty() ? mCfg.snaps->RollbackLast(lg)
                           : mCfg.snaps->RollbackThrough(id, lg);
      Log(ok ? "info" : "warn", "Откат " + (id.empty() ? std::string("(последний)") : id) + ":\n" + lg);
      mCfg.project->MarkDirty();
      return nlohmann::json{{"ok", ok}, {"log", lg}};
    });
  });

  srv.Post("/api/snapshots/forget", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json& body) {
      std::string id = body.value("id", "");
      bool ok = mCfg.snaps->Delete(id);
      return nlohmann::json{{"ok", ok}};
    });
  });

  // --- чат ---
  srv.Post("/api/chat/connect", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) {
      std::string err = mCfg.chat->ConnectNow();
      return nlohmann::json{{"ok", err.empty()}, {"error", err}};
    });
  });

  srv.Post("/api/chat/send", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json& body) {
      std::string text = body.value("text", "");
      std::string kind = body.value("kind", "task");
      std::string err;
      bool ok = (kind == "note") ? mCfg.chat->SendNote(text, err)
                                 : mCfg.chat->SendTask(text, err);
      return nlohmann::json{{"ok", ok}, {"error", err}};
    });
  });

  srv.Get("/api/chat/reply", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) {
      ParsedOps parsed;
      if (!mCfg.chat->TakeReply(parsed)) return nlohmann::json{{"ready", false}};
      nlohmann::json ops = nlohmann::json::array();
      for (auto& op : parsed.ops)
        ops.push_back({{"name", op.value("name", "")},
                       {"args", op.value("args", nlohmann::json::object())},
                       {"describe", ToolRegistry::Describe(op.value("name", ""), op.value("args", nlohmann::json::object()))}});
      return nlohmann::json{{"ready", true},
                            {"text", parsed.text},
                            {"ops", ops},
                            {"errs", parsed.errors}};
    });
  });

  srv.Post("/api/chat/apply", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json& body) {
      if (!mCfg.project->IsOpen()) return nlohmann::json{{"error", "проект не открыт"}};
      std::ostringstream report;
      if (mCfg.snaps->EnabledForProject()) mCfg.snaps->Begin("DeepSeekIDE: правки из веб-чата");
      int okCount = 0;
      const auto& arr = body["ops"];
      if (!arr.is_array()) return nlohmann::json{{"error", "ops должен быть массивом"}};
      for (const auto& op : arr) {
        std::string name = op.value("name", "");
        nlohmann::json args = op.value("args", nlohmann::json::object());
        if (dside::MutationOps().count(name) == 0) {
          report << "✗ " << name << " — операция не из белого списка\n";
          continue;
        }
        ToolRunResult r = mCfg.tools->Execute(name, args);
        report << (r.ok ? "✓ " : "✗ ") << ToolRegistry::Describe(name, args);
        if (!r.ok) {
          std::string o = r.output;
          if (o.size() > 160) o = o.substr(0, 157) + "…";
          report << " — " << o;
        }
        report << "\n";
        if (r.ok) ++okCount;
      }
      if (mCfg.snaps->EnabledForProject()) mCfg.snaps->Commit();
      report << "\nГотово: " << okCount << " из " << arr.size() << " операций успешно.";
      Log("agent", "Применение операций:\n" + report.str());
      mCfg.project->MarkDirty();
      return nlohmann::json{{"ok", true}, {"report", report.str()}, {"done", okCount},
                            {"total", arr.size()}};
    });
  });

  srv.Post("/api/chat/cancel", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) {
      mCfg.chat->CancelWait();
      return nlohmann::json{{"ok", true}};
    });
  });

  srv.Post("/api/chat/newchat", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) {
      mCfg.chat->NewChat();
      return nlohmann::json{{"ok", true}};
    });
  });

  srv.Post("/api/shutdown", [this, api](const httplib::Request& req, httplib::Response& res) {
    api(req, res, [this](const nlohmann::json&) {
      Log("info", "Получена команда завершения");
      std::thread([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (mImpl) mImpl->shuttingDown.store(true);
        mImpl->srv.stop();
      }).detach();
      return nlohmann::json{{"ok", true}};
    });
  });

  // Запуск потока слушалки
  mImpl->th = std::thread([this] {
    if (!mImpl->srv.listen("127.0.0.1", mPort)) {
      Log("error", "Не удалось занять порт " + std::to_string(mPort));
    }
  });

  // Подтверждение: сервер поднялся
  for (int i = 0; i < 40; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (mImpl->srv.is_running()) break;
  }
  if (!mImpl->srv.is_running()) {
    errOut = "HTTP-сервер не стартовал на 127.0.0.1:" + std::to_string(mPort);
    return false;
  }
  return true;
}

void IdeServer::Stop() {
  if (!mImpl) return;
  mImpl->srv.stop();
  if (mImpl->th.joinable()) mImpl->th.join();
  mImpl.reset();
}

// ---------------------------------------------------------------------------

nlohmann::json IdeServer::StateJson() const {
  nlohmann::json j;
  j["project"]["open"] = mCfg.project->IsOpen();
  if (mCfg.project->IsOpen()) {
    j["project"]["root"] = mCfg.project->RootStr();
    j["project"]["tree"] = NodeJson(mCfg.project->Tree());
  }
  auto st = mCfg.chat->GetStatus();
  j["chat"] = {{"stage", st.stage},     {"stageText", st.stageText},
               {"browser", st.browser}, {"error", st.error},
               {"chatPresent", st.chatPresent}, {"busy", st.busy},
               {"replyReady", mCfg.chat->ReplyReady()}};
  j["dirty"] = mCfg.project->ConsumeDirty();  // веб-фронт сам перезапросит дерево
  return j;
}

nlohmann::json IdeServer::SnapshotsJson() const {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& s : mCfg.snaps->List()) {
    nlohmann::json files = nlohmann::json::array();
    for (const auto& f : s.files) files.push_back(f.path);
    arr.push_back({{"id", s.id},       {"title", s.title},
                   {"iso", s.isoTime}, {"atMs", s.epochMs},
                   {"files", files}});
  }
  return nlohmann::json{{"enabled", mCfg.snaps->EnabledForProject()},
                        {"snapshots", arr}};
}

std::string IdeServer::ServeFile(const std::string& rel, std::string& contentTypeOut,
                                 bool& okOut) const {
  okOut = false;
  std::string cleaned = rel;
  // защита от выхода из корня web-папки
  if (cleaned.find("..") != std::string::npos) return {};
  while (!cleaned.empty() && cleaned[0] == '/') cleaned.erase(cleaned.begin());
  auto abs = mCfg.webRoot / cleaned;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(abs, ec)) return {};
  std::ifstream f(abs, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  contentTypeOut = MimeFor(cleaned);
  okOut = true;
  return ss.str();
}
