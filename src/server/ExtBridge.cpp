// ExtBridge — серверная сторона браузерного расширения.

#include "server/ExtBridge.h"

#include <sstream>

#include "ai/AutoNote.h"
#include "ai/OpsParser.h"
#include "ai/Prompt.h"
#include "ai/ToolRegistry.h"
#include "app/Platform.h"
#include "core/ProjectManager.h"
#include "core/SnapshotManager.h"
#include "server/OpsApply.h"
#include "util/Utf8.h"

void ExtBridge::Init(const Cfg& cfg) { mCfg = cfg; }

nlohmann::json ExtBridge::HomeJson() const {
  return nlohmann::json{{"url", "http://127.0.0.1:" + std::to_string(mCfg.port) + "/"}};
}

nlohmann::json ExtBridge::StateJson() {
  std::lock_guard<std::mutex> lk(mMtx);
  return nlohmann::json{{"ok", true},
                        {"autopilot", mAutopilot},
                        {"lastSeenMs", mLastSeenMs},
                        {"online", platform::NowMillis() - mLastSeenMs < 15000},
                        {"answers", mAnswers},
                        {"opsApplied", mOpsApplied},
                        {"notesSent", mNotesSent}};
}

nlohmann::json ExtBridge::PromptJson() {
  {
    std::lock_guard<std::mutex> lk(mMtx);
    mLastSeenMs = platform::NowMillis();
  }
  std::string tree = "(проект не открыт — дерево недоступно)";
  if (mCfg.tools) {
    ToolRunResult r = mCfg.tools->Execute("list_files", {{"max_entries", 350}});
    if (!r.output.empty()) tree = utf8::Sanitize(r.output);
  }
  return nlohmann::json{{"ok", true}, {"prompt", dside::BuildAgentPrompt(tree, "")}};
}

nlohmann::json ExtBridge::ResetDialog() {
  std::lock_guard<std::mutex> lk(mMtx);
  mSentFiles.clear();
  mSentSearches.clear();
  return nlohmann::json{{"ok", true}};
}

nlohmann::json ExtBridge::SetAutopilot(const nlohmann::json& body) {
  std::lock_guard<std::mutex> lk(mMtx);
  mAutopilot = body.value("enabled", true);
  return nlohmann::json{{"ok", true}, {"autopilot", mAutopilot}};
}

nlohmann::json ExtBridge::HandleAnswer(const nlohmann::json& body) {
  const std::string raw = body.value("text", "");
  if (raw.empty()) return nlohmann::json{{"error", "пустой text"}};

  bool autopilot = false;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    mLastSeenMs = platform::NowMillis();
    mAnswers++;
    autopilot = mAutopilot;
  }

  // --- разбор ops: DOM-путь (obs — тела блоков deepseekide-ops из страницы),
  //     текстовый фолбэк (заборы ``` в innerText). Как в ChatDriver-settle.
  ParsedOps parsed;
  bool usedDom = false;
  if (body.contains("obs") && body["obs"].is_array()) {
    for (const auto& b : body["obs"]) {
      if (!b.is_string()) continue;
      dside::ParseOpsBody(b.get<std::string>(), dside::ChatAllowedOps(),
                          static_cast<int>(parsed.ops.size() + 1), parsed);
      usedDom = true;
    }
  }
  if (usedDom) {
    if (parsed.text.empty()) parsed.text = raw;
  } else {
    dside::ExtractOps(raw, dside::ChatAllowedOps(), parsed);
  }

  // --- авто-применение
  int applied = 0;
  std::string report, runNote;
  if (autopilot && !parsed.ops.empty() && mCfg.project && mCfg.project->IsOpen()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& o : parsed.ops) arr.push_back(o);
    applied = ApplyChatOps(
        arr, *mCfg.tools, report, runNote,
        [this] {
          if (mCfg.snaps && mCfg.snaps->EnabledForProject())
            mCfg.snaps->Begin("DeepSeekIDE: автоправки (расширение)");
        },
        [this] {
          if (mCfg.snaps && mCfg.snaps->EnabledForProject()) mCfg.snaps->Commit();
        });
    if (mCfg.project) mCfg.project->MarkDirty();
    {
      std::lock_guard<std::mutex> lk(mMtx);
      mOpsApplied += applied;
      mLastReport = report;
    }
  }

  // --- заметки обратно в чат: 1) отчёт о применении 2) самообслуживание
  std::string note;
  if (autopilot && !parsed.ops.empty()) {
    std::ostringstream rep;
    rep << "SYSTEM: ops applied automatically by DeepSeekIDE:\n\n```\n" << report;
    if (!runNote.empty()) rep << "\nrun_command output:\n" << runNote;
    rep << "\n```\n";
    note = rep.str();
  }
  // Дедуп-сеты мутирует сама CollectServiceNote; параллельный ResetDialog
  // в худшем случае даст дубль заметки — безвредно, поэтому без мьютекса
  // (но и без долгого удержания лока).
  if (mCfg.tools) {
    std::string logInfo;  // текст для журнала отдают эндпойнты
    std::string serviceNote = dside::CollectServiceNote(mCfg.tools, raw, mSentFiles,
                                                        mSentSearches, logInfo);
    if (!serviceNote.empty()) {
      if (!note.empty()) note += "\n\n";
      note += serviceNote;
    }
  }

  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (!note.empty()) mNotesSent++;
  }

  return nlohmann::json{{"ok", true},
                        {"ops", parsed.ops.size()},
                        {"applied", applied},
                        {"report", report},
                        {"note", note},
                        {"errors", parsed.errors.size()},
                        {"autopilot", autopilot}};
}
