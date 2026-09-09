#include "ai/AgentBridge.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

#include "ai/ToolRegistry.h"
#include "core/ProjectManager.h"
#include "core/SnapshotManager.h"
#include "ui/WebChatPanel.h"

#include "ai/OpsParser.h"

namespace {

const std::set<std::string>& AllowedOps() { return dside::MutationOps(); }

}  // namespace

void AgentBridge::Wire(WebChatPanel* panel, ToolRegistry* tools, SnapshotManager* snaps,
                       ProjectManager* proj) {
  mPanel = panel;
  mTools = tools;
  mSnaps = snaps;
  mProj = proj;
}

long long AgentBridge::NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::set<std::string> AgentBridge::KnownOpNames() const { return AllowedOps(); }

// ---------------------------------------------------------------------------

bool AgentBridge::ParseOps(const std::string& text, const std::set<std::string>& known,
                           ReplyInfo& out) {
  ParsedOps parsed;
  dside::ExtractOps(text, known, parsed);
  out = ReplyInfo{};
  out.text = std::move(parsed.text);
  out.errs = std::move(parsed.errors);
  for (auto& j : parsed.ops)
    out.ops.push_back(Op{j.value("name", std::string{}), j.value("args", nlohmann::json{})});
  return !out.ops.empty();
}

// ---------------------------------------------------------------------------

std::string AgentBridge::BuildPrompt(const std::string& task) {
  std::string tree = "(проект не открыт — дерево недоступно)";
  if (mTools && mProj && mProj->IsOpen()) {
    ToolRunResult r = mTools->Execute("list_files", {{"max_entries", 350}});
    if (!r.output.empty()) tree = r.output;
  }

  std::ostringstream p;
  p << "Вы — агент программирования, встроенный в IDE DeepSeekIDE на моём компьютере. "
       "Отвечайте по-русски.\n\n"
       "ВАЖНЕЙШЕЕ ПРАВИЛО ФОРМАТА:\n"
       "Каждое изменение файлов оформляйте ТОЛЬКО блоком вида:\n"
       "```deepseekide-ops\n"
       "[{\"name\":\"write_file\",\"args\":{\"path\":\"src/main.cpp\",\"content\":\"полный текст "
       "файла\"}}]\n"
       "```\n"
       "Блоков может быть несколько — они выполняются сверху вниз. ВНУТРИ блока — только валидный "
       "JSON: один объект {\"name\": ..., \"args\": {...}} или массив таких объектов. Никакого текста, "
       "комментариев или пояснений внутри блока. Переводы строк в content — как \\n.\n\n"
       "ДОСТУПНЫЕ ОПЕРАЦИИ:\n"
       "- write_file {path, content} — создать или ПОЛНОСТЬЮ перезаписать файл\n"
       "- edit_file {path, old_string, new_string, replace_all?} — замена ТОЧНОГО фрагмента (текст "
       "должен совпадать посимвольно, включая отступы)\n"
       "- append_file {path, content} — дописать в конец файла\n"
       "- insert_lines {path, line, content} — вставить текст ПЕРЕД строкой line (нумерация с 1; "
       "line = число строк + 1 — в конец файла)\n"
       "- replace_lines {path, start_line, end_line, content} — заменить строки start..end на новый "
       "текст (content может быть пустым — тогда строки удаляются)\n"
       "- make_dir {path} — создать папку\n"
       "- delete_path {path, recursive?} — удалить файл; для папки — recursive:true\n"
       "- copy_file {from, to} — скопировать файл\n"
       "- move_file {from, to} — переместить/переименовать\n\n"
       "Если вам нужно СОДЕРЖИМОЕ существующего файла — напишите обычным текстом вне блоков: "
       "«НУЖЕН ФАЙЛ: <путь>», и я пришлю его следующим сообщением.\n"
       "Все пояснения — обычным текстом ВНЕ блоков deepseekide-ops.\n\n"
       "СТРУКТУРА ПРОЕКТА:\n"
       << tree << "\n\n"
        "ЗАДАЧА:\n"
        << task;
  return p.str();
}

// ---------------------------------------------------------------------------

bool AgentBridge::DoSend(const std::string& text, std::string& errorOut) {
  if (!mPanel || !mPanel->Supported()) {
    errorOut = "Веб-модуль не собран (DEEPSEEKIDE_WEBVIEW=OFF) — встроенный чат недоступен.";
    return false;
  }
  if (!mPanel->Running()) {
    errorOut = "Встроенный браузер чата ещё запускается… повторите через пару секунд.";
    return false;
  }
  if (mPhase != Phase::Idle) {
    errorOut = "Уже жду ответ на предыдущее сообщение. Дождитесь или нажмите «Сброс».";
    return false;
  }
  // Убедимся, что мост на странице жив — иначе смысла слать нет.
  const auto st = mPanel->GetState();
  if (!st.ok) {
    errorOut = "Скрипт-мост пока не подключился к странице chat.deepseek.com. "
               "Если просит капчу/вход — решите её прямо в окне чата слева и повторите.";
    return false;
  }
  if (!st.chatPresent) {
    errorOut = "Не вижу поле ввода на странице chat.deepseek.com — войдите в учётную запись "
               "в окне чата слева и повторите.";
    return false;
  }
  mBaseline = st.messages;
  mSentAtMs = NowMs();
  mSettleSinceMs = 0;
  mLastSeenBusyMs = 0;
  mError.clear();
  mPendingText = text;
  mSendRetries = 0;
  mPanel->SendPrompt(text);
  mPhase = Phase::Sending;
  return true;
}

bool AgentBridge::SendTask(const std::string& task, std::string& errorOut) {
  return DoSend(BuildPrompt(task), errorOut);
}

bool AgentBridge::SendNote(const std::string& text, std::string& errorOut) {
  return DoSend(text, errorOut);
}

void AgentBridge::Cancel() {
  mPhase = Phase::Idle;
  mError.clear();
  mReplyTaken = true;
}

// ---------------------------------------------------------------------------

void AgentBridge::Pump() {
  if (!mPanel || mPhase == Phase::Idle || mPhase == Phase::Ready || mPhase == Phase::TimedOut)
    return;

  const auto st = mPanel->GetState();
  const long long now = NowMs();

  if (mPhase == Phase::Sending) {
    if (!st.ok) {
      if (now - mSentAtMs > 20000) {
        mError = "Страница чата не отвечает скрипту-мосту. Откройте chat.deepseek.com слева, "
                 "авторизуйтесь и отправьте задачу ещё раз.";
        mPhase = Phase::TimedOut;
      }
      return;
    }
    // Страница «ожила» или выросло число ответов → переходим к ожиданию завершения.
    if (st.messages > mBaseline || st.busy) {
      mPhase = Phase::WaitingSettle;
      mLastSeenBusyMs = now;
    } else if (now - mSentAtMs > 15000) {
      // Возможно, клик по кнопке не сработал — пробуем ещё раз, максимум трижды.
      if (mSendRetries < 3) {
        ++mSendRetries;
        mPanel->SendPrompt(mPendingText);
        mSentAtMs = now;
      } else {
        mError = "Чат не принял сообщение (кнопка отправки не нажалась). "
                 "Отправьте текст вручную или начните новый чат.";
        mPhase = Phase::TimedOut;
      }
    }
    return;
  }

  if (mPhase == Phase::WaitingSettle) {
    if (!st.chatPresent) {
      mError = "Страница чата перестала отвечать (перелогин?). Повторите отправку.";
      mPhase = Phase::TimedOut;
      return;
    }
    if (st.busy) {
      mSettleSinceMs = 0;
    } else if (mSettleSinceMs == 0) {
      mSettleSinceMs = now;
    }
    if (mSettleSinceMs != 0 && now - mSettleSinceMs > 1800) {
      // Генерация закончилась: фиксируем ответ.
      mReply = ReplyInfo{};
      mReply.receivedAtMs = now;
      ReplyInfo parsed;
      std::string raw = st.last;
      ParseOps(raw, AllowedOps(), parsed);
      mReply = std::move(parsed);
      mReply.receivedAtMs = now;
      if (mReply.text.empty() && mReply.ops.empty() && raw.empty())
        mReply.text = "(пустой ответ — возможно, страница не успела отрендерить)";
      mPhase = Phase::Ready;
      mReplyTaken = false;
      return;
    }
    if (now - mSentAtMs > 6 * 60 * 1000) {
      mError = "Чат не ответил за 6 минут — снято ожидание. Можно снять ответ кнопкой «Забрать "
               "текущий ответ» или попробовать снова.";
      mPhase = Phase::TimedOut;
    }
  }
}

bool AgentBridge::TakeReadyReply(ReplyInfo& out) {
  if ((mPhase != Phase::Ready && mPhase != Phase::TimedOut) || mReplyTaken) return false;
  out = mReply;
  mReplyTaken = true;
  mPhase = Phase::Idle;
  return true;
}

std::string AgentBridge::StatusText() const {
  switch (mPhase) {
    case Phase::Idle: return "Готов";
    case Phase::Sending: return "Отправлено в чат…";
    case Phase::WaitingSettle: return "Думаю над ответом…";
    case Phase::Ready: return "Ответ получен";
    case Phase::TimedOut: return "Ждать ответа не удалось";
  }
  return "?";
}

// ---------------------------------------------------------------------------

std::string AgentBridge::ApplyOps(const std::vector<Op>& ops) {
  std::ostringstream report;
  if (!mTools || !mProj || !mProj->IsOpen()) {
    report << "Проект не открыт — применять некуда.";
    return report.str();
  }
  if (ops.empty()) {
    report << "Операций нет.";
    return report.str();
  }

  if (mSnaps && mSnaps->EnabledForProject()) mSnaps->Begin("DeepSeekIDE: правки из веб-чата");
  int okCount = 0;
  for (size_t i = 0; i < ops.size(); ++i) {
    const Op& op = ops[i];
    ToolRunResult r = mTools->Execute(op.name, op.args);
    report << (r.ok ? "✓ " : "✗") << " " << ToolRegistry::Describe(op.name, op.args);
    if (!r.ok) {
      std::string o = r.output;
      if (o.size() > 160) o = o.substr(0, 157) + "…";
      report << " — " << o;
    }
    report << "\n";
    if (r.ok) ++okCount;
  }
  if (mSnaps && mSnaps->EnabledForProject()) mSnaps->Commit();
  report << "\nГотово: " << okCount << " из " << ops.size() << " операций успешно.";
  return report.str();
}
