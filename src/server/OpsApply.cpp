// OpsApply — ядро применения операций чат-агента (см. .h).

#include "server/OpsApply.h"

#include <sstream>

#include "util/Utf8.h"

int ApplyChatOps(const nlohmann::json& ops, ToolRegistry& tools, std::string& report,
                 std::string& runNote, const std::function<void()>& snapBegin,
                 const std::function<void()>& snapCommit, const std::set<std::string>* allowed,
                 const std::function<void(const std::string& name, const ToolRunResult& r)>& diag) {
  const auto& allow = allowed ? *allowed : dside::ChatAllowedOps();
  std::ostringstream rep, rn;
  if (snapBegin) snapBegin();
  int okCount = 0;
  const size_t total = ops.is_array() ? ops.size() : 0;
  if (!ops.is_array()) {
    report = "ops должен быть массивом";
    return 0;
  }
  for (const auto& op : ops) {
    const std::string name = op.is_object() ? op.value("name", "") : "";
    const nlohmann::json args =
        op.is_object() && op.contains("args") && op["args"].is_object()
            ? op["args"]
            : nlohmann::json::object();
    if (name.empty() || allow.count(name) == 0) {
      rep << "✗ " << (name.empty() ? "(без имени)" : name) << " — операция не из белого списка\n";
      continue;
    }
    ToolRunResult r = tools.Execute(name, args);
    if (diag) diag(name, r);
    if (name == "run_command") {
      // Модели без вывода команды работать вслепую: возвращаем stdout/stderr
      // автосообщением (кириллица CP866/битый UTF-8 проходит санитизацию).
      rn << "$ " << utf8::Sanitize(args.value("command", "")) << "\n(" << (r.ok ? "ok" : "error")
         << ")\n```\n";
      std::string out = utf8::Sanitize(r.output);
      if (out.size() > 20000) out = utf8::Truncate(out, 20000) + "\n…(truncated)\n";
      rn << out << "\n```\n\n";
    }
    rep << (r.ok ? "✓ " : "✗ ") << ToolRegistry::Describe(name, args);
    if (!r.ok) {
      // Вывод консоли (на Windows нередко CP866 и битый UTF-8) + срез
      // строго по границе символа.
      std::string o = utf8::Sanitize(r.output);
      if (o.size() > 160) o = utf8::Truncate(o, 157) + "…";
      rep << " — " << o;
    }
    rep << "\n";
    if (r.ok) ++okCount;
  }
  if (snapCommit) snapCommit();
  rep << "\nГотово: " << okCount << " из " << total << " операций успешно.";
  report = rep.str();
  runNote = rn.str();
  return okCount;
}
