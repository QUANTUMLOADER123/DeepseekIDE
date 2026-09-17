// Самообслуживание модели (NEED FILE / NEED SEARCH) — см. заголовок.

#include "ai/AutoNote.h"

#include <sstream>
#include <vector>

#include "ai/OpsParser.h"
#include "ai/ToolRegistry.h"
#include "util/Utf8.h"

std::string dside::CollectServiceNote(ToolRegistry* tools, const std::string& answerRaw,
                                      std::set<std::string>& sentFiles,
                                      std::set<std::string>& sentSearches, std::string& logOut) {
  logOut.clear();
  if (!tools) return "";
  const auto wants = dside::FindFileRequests(answerRaw);
  const auto searches = dside::FindSearchRequests(answerRaw);
  if (wants.empty() && searches.empty()) return "";

  std::vector<std::string> todoFiles, todoSearches;
  for (const auto& w : wants)
    if (sentFiles.insert(w).second) todoFiles.push_back(w);
  for (const auto& q : searches)
    if (sentSearches.insert(q).second) todoSearches.push_back(q);
  if (todoFiles.empty() && todoSearches.empty()) {
    logOut = "Модель ждёт файлы/поиск, которые уже отправлены — не дублирую.";
    return "";
  }

  std::ostringstream note;
  note << "SYSTEM: auto-reply from DeepSeekIDE.\n\n";
  int sent = 0;
  for (const auto& w : todoFiles) {
    if (++sent > 3) {
      note << "(more files pending — repeat NEED FILE for them to continue)\n";
      break;
    }
    note << "You requested file content of Â«" << w << "Â» (lines are numbered; "
            "use those numbers with insert_lines/replace_lines).\n";
    ToolRunResult fr = tools->Execute("read_file", {{"path", w}});
    std::string body = utf8::Sanitize(fr.output);
    if (body.size() > 120000)
      body = utf8::Truncate(body, 120000) + "\n...(truncated - first 120000 chars shown)\n";
    note << "FILE \"" << w << "\"" << (fr.ok ? "" : " - READ ERROR: ")
         << "\n```\n" << body << "\n```\n\n";
  }
  int ssent = 0;
  for (const auto& q : todoSearches) {
    if (++ssent > 2) {
      note << "(more searches pending - repeat NEED SEARCH for them)\n";
      break;
    }
    note << "You searched the project for Â«" << q << "Â». Matches (file:line: text):\n";
    ToolRunResult sr = tools->Execute("search_files", {{"query", q}});
    std::string body = utf8::Sanitize(sr.output);
    if (body.size() > 60000) body = utf8::Truncate(body, 60000) + "\n...(truncated)\n";
    note << "SEARCH \"" << q << "\"" << (sr.ok ? "" : " - ERROR: ")
         << "\n```\n" << body << "\n```\n\n";
  }
  logOut = "Модель попросила " + std::to_string(todoFiles.size()) + " файл(ов), " +
           std::to_string(todoSearches.size()) + " поиск(ов) — отправляю автоматически в чат.";
  return note.str();
}
