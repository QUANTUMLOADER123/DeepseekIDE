#include "ai/ToolRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>
#include <system_error>

#include "app/Platform.h"
#include "core/ProjectManager.h"
#include "core/SnapshotManager.h"

namespace fs = std::filesystem;

namespace {

nlohmann::json Tool(const char* name, const char* description, nlohmann::json properties,
                    std::vector<std::string> required) {
  return {{"type", "function"},
          {"function",
           {{"name", name},
            {"description", description},
            {"parameters",
             {{"type", "object"},
              {"properties", std::move(properties)},
              {"required", std::move(required)}}}}}};
}

nlohmann::json Prop(const char* type, const char* description) {
  return {{"type", type}, {"description", description}};
}

std::string Lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool LooksText(const std::string& name) {
  static const char* exts[] = {".txt", ".md",  ".c",    ".h",   ".cpp", ".hpp",  ".cc",  ".cxx",
                               ".py",  ".js",  ".ts",   ".tsx", ".jsx", ".json", ".xml", ".yml",
                               ".yaml",".toml",".ini",  ".cfg", ".cs",  ".java", ".go",  ".rs",
                               ".lua", ".sql", ".html", ".css", ".scss",".sh",   ".bat", ".ps1",
                               ".cmake",".glsl",".hlsl",".rb",  ".php", ".swift",".kt",  ".vim"};
  std::string low = Lower(name);
  if (low.size() >= 4 && low.substr(low.size() - 7) == "akefile") return true;  // Makefile
  for (auto* e : exts)
    if (low.size() > strlen(e) && low.compare(low.size() - strlen(e), std::string::npos, e) == 0)
      return true;
  return false;
}

}  // namespace

ToolRegistry::ToolRegistry(ToolContext ctx) : mCtx(std::move(ctx)) { BuildSchemas(); }

std::string ToolRegistry::Arg(const nlohmann::json& args, const char* key,
                              const std::string& def) const {
  if (!args.is_object() || !args.contains(key)) return def;
  const auto& v = args[key];
  if (v.is_string()) return v.get<std::string>();
  if (v.is_number_integer()) return std::to_string(v.get<long long>());
  if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
  return def;
}

bool ToolRegistry::Resolve(const std::string& rel, fs::path& out, ToolRunResult& err,
                           bool forWrite) const {
  if (!mCtx.project || !mCtx.project->IsOpen()) {
    err.ok = false;
    err.output = "Папка проекта не открыта. Попросите пользователя открыть папку проекта.";
    return false;
  }
  if (forWrite && ProjectManager::IsProtectedRel(rel)) {
    err.ok = false;
    err.output = "Доступ запрещён: путь внутри служебной папки (.git/.deepseekide): " + rel;
    return false;
  }
  std::string e;
  if (!ProjectManager::SafeJoin(mCtx.project->Root(), rel, out, e)) {
    err.ok = false;
    err.output = e;
    return false;
  }
  return true;
}

void ToolRegistry::BuildSchemas() {
  mSchemas = nlohmann::json::array();
  mSchemas.push_back(Tool(
      "list_files",
      "Показать дерево файлов и папок проекта. path — относительный путь подпапки "
      "(по умолчанию весь проект). Используй, чтобы изучить структуру перед изменениями.",
      {{"path", Prop("string", "Относительный путь к папке (по умолчанию — корень)")},
       {"max_entries", Prop("integer", "Максимум строк результата (по умолчанию 400)")}},
      {}));
  mSchemas.push_back(Tool(
      "read_file",
      "Прочитать текстовый файл проекта. Возвращает содержимое с номерами строк. "
      "Для больших файлов указывай start_line/end_line.",
      {{"path", Prop("string", "Относительный путь к файлу")},
       {"start_line", Prop("integer", "Первая строка (с 1, необязательно)")},
       {"end_line", Prop("integer", "Последняя строка (включительно, необязательно)")}},
      {"path"}));
  mSchemas.push_back(Tool(
      "write_file",
      "Создать новый файл или ПОЛНОСТЬЮ перезаписать существующий. Родительские папки "
      "создаются автоматически. Перед перезаписью существующего сначала прочитай его.",
      {{"path", Prop("string", "Относительный путь к файлу")},
       {"content", Prop("string", "Полное новое содержимое файла")}},
      {"path", "content"}));
  mSchemas.push_back(Tool(
      "edit_file",
      "Точечно изменить файл: заменить ТОЧНЫЙ фрагмент old_string на new_string. "
      "Фрагмент должен встречаться в файле ровно так, как указан (включая отступы и "
      "переводы строк). Для одной правки — replace_all=false, для массовой — true.",
      {{"path", Prop("string", "Относительный путь к файлу")},
       {"old_string", Prop("string", "Точный заменяемый фрагмент")},
       {"new_string", Prop("string", "Новый текст вместо фрагмента")},
       {"replace_all", Prop("boolean", "Заменить все вхождения (по умолчанию false)")}},
      {"path", "old_string", "new_string"}));
  mSchemas.push_back(Tool(
      "make_dir", "Создать папку (включая вложенные).",
      {{"path", Prop("string", "Относительный путь к папке")}}, {"path"}));
  mSchemas.push_back(Tool(
      "delete_path",
      "Удалить файл. Для папки укажи recursive=true (содержимое сохраняется в снимок).",
      {{"path", Prop("string", "Относительный путь")},
       {"recursive", Prop("boolean", "Удалить папку рекурсивно (по умолчанию false)")}},
      {"path"}));
  mSchemas.push_back(Tool(
      "search_files",
      "Найти подстроку в текстовых файлах проекта. Возвращает совпадения в формате "
      "путь:строка: текст. Удобно для поиска функций, переменных, TODO.",
      {{"query", Prop("string", "Что искать (подстрока)")},
       {"path", Prop("string", "В какой подпапке искать (по умолчанию везде)")},
       {"case_sensitive", Prop("boolean", "Учитывать регистр (по умолчанию false)")}},
      {"query"}));
  mSchemas.push_back(Tool(
      "run_command",
      "Выполнить консольную команду в корне проекта (сборка, тесты, git status и т.п.). "
      "Работает только если пользователь разрешил shell в настройках.",
      {{"command", Prop("string", "Команда для оболочки")},
       {"timeout_sec", Prop("integer", "Таймаут в секундах (по умолчанию из настроек)")}},
      {"command"}));
}

std::string ToolRegistry::Describe(const std::string& name, const nlohmann::json& args) {
  auto a = [&](const char* k) {
    std::string v;
    if (args.is_object() && args.contains(k) && args[k].is_string())
      v = args[k].get<std::string>();
    if (v.size() > 48) v = v.substr(0, 45) + "...";
    return v;
  };
  if (name == "run_command") return name + " · " + a("command");
  if (name == "search_files") return name + " · «" + a("query") + "»";
  return name + " · " + a("path");
}

ToolRunResult ToolRegistry::Execute(const std::string& name, const nlohmann::json& args) {
  if (name == "list_files") return ListFiles(args);
  if (name == "read_file") return ReadFile(args);
  if (name == "write_file") return WriteFile(args);
  if (name == "edit_file") return EditFile(args);
  if (name == "make_dir") return MakeDir(args);
  if (name == "delete_path") return DeletePath(args);
  if (name == "search_files") return SearchFiles(args);
  if (name == "run_command") return RunCommand(args);
  ToolRunResult r;
  r.output = "Неизвестный инструмент: " + name;
  return r;
}

// ---------------------------------------------------------------------------

ToolRunResult ToolRegistry::ListFiles(const nlohmann::json& args) {
  ToolRunResult r;
  std::string sub = Arg(args, "path", ".");
  int maxEntries = args.value("max_entries", 400);
  fs::path abs;
  if (!Resolve(sub.empty() ? "." : sub, abs, r, false)) return r;

  std::error_code ec;
  if (!fs::is_directory(abs, ec)) {
    r.output = "Папка не найдена: " + sub;
    return r;
  }

  const std::string rootStr = platform::PathToStr(mCtx.project->Root());
  std::ostringstream out;
  int count = 0;
  std::function<void(const fs::path&, int)> walk = [&](const fs::path& dir, int depth) {
    if (count >= maxEntries || depth > 10) return;
    std::vector<fs::path> entries;
    for (auto it = fs::directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec))
      entries.push_back(it->path());
    std::sort(entries.begin(), entries.end(), [](const fs::path& a, const fs::path& b) {
      std::error_code e1, e2;
      bool da = fs::is_directory(a, e1), db = fs::is_directory(b, e2);
      if (da != db) return da > db;
      return a.filename() < b.filename();
    });
    for (const auto& p : entries) {
      if (count >= maxEntries) return;
      std::string name = platform::PathToStr(p.filename());
      bool isDir = fs::is_directory(p, ec);
      if (isDir) {
        static const std::set<std::string> ig = {".git", ".deepseekide", "node_modules",
                                                 "build", "dist", ".venv", "__pycache__"};
        if (ig.count(name)) continue;
      }
      out << std::string(static_cast<size_t>(depth) * 2, ' ') << (isDir ? "- " : "  ") << name
          << (isDir ? "/" : "") << "\n";
      ++count;
      if (isDir) walk(p, depth + 1);
    }
  };
  walk(abs, 0);
  out << "(показано " << count << " записей; корень проекта: " << rootStr << ")";
  r.ok = true;
  r.output = out.str();
  return r;
}

ToolRunResult ToolRegistry::ReadFile(const nlohmann::json& args) {
  ToolRunResult r;
  fs::path abs;
  if (!Resolve(Arg(args, "path"), abs, r, false)) return r;

  std::error_code ec;
  if (fs::is_directory(abs, ec)) {
    r.output = "Это папка, а не файл. Используйте list_files.";
    return r;
  }
  auto size = fs::file_size(abs, ec);
  if (!ec && size > 1024 * 1024) {
    r.output = "Файл больше 1 МБ — читайте частями (start_line/end_line) или уточните, что нужно.";
    return r;
  }
  std::string text;
  if (!platform::ReadTextFile(abs, text)) {
    r.output = "Не удалось прочитать файл (возможно, он бинарный или не существует).";
    return r;
  }

  int start = args.value("start_line", 1);
  int end = args.value("end_line", 1 << 30);
  std::istringstream in(text);
  std::ostringstream out;
  std::string line;
  int n = 0, shown = 0;
  const int kMaxShown = 4000;
  while (std::getline(in, line)) {
    ++n;
    if (n < start) continue;
    if (n > end || shown >= kMaxShown) continue;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    char num[16];
    std::snprintf(num, sizeof(num), "%5d | ", n);
    out << num << line << "\n";
    ++shown;
  }
  if (shown == 0 && n == 0) out << "(файл пуст)\n";
  out << "-- конец выборки: строк всего " << n << ", показано " << shown;
  if (shown >= kMaxShown) out << " (обрезано — читайте по диапазонам)";
  r.ok = true;
  r.output = out.str();
  return r;
}

ToolRunResult ToolRegistry::WriteFile(const nlohmann::json& args) {
  ToolRunResult r;
  const std::string rel = Arg(args, "path");
  const std::string content = Arg(args, "content");
  fs::path abs;
  if (!Resolve(rel, abs, r, true)) return r;

  std::error_code ec;
  bool existed = fs::exists(abs, ec);
  if (mCtx.snapshots) mCtx.snapshots->RecordFileChange(abs);

  std::string werr;
  if (!platform::WriteTextFile(abs, content, &werr)) {
    r.output = "Ошибка записи: " + werr;
    return r;
  }
  r.ok = true;
  std::ostringstream out;
  out << (existed ? "Перезаписан файл " : "Создан файл ") << rel << " ("
      << std::count(content.begin(), content.end(), '\n') + 1 << " строк, "
      << platform::HumanSize(content.size()) << ")";
  r.output = out.str();
  if (mCtx.project) mCtx.project->MarkDirty();
  if (mCtx.fileChanged) mCtx.fileChanged(rel);
  if (mCtx.log) mCtx.log("tool", r.output);
  return r;
}

ToolRunResult ToolRegistry::EditFile(const nlohmann::json& args) {
  ToolRunResult r;
  const std::string rel = Arg(args, "path");
  const std::string oldS = Arg(args, "old_string");
  const std::string newS = Arg(args, "new_string");
  const bool replaceAll = args.value("replace_all", false);
  fs::path abs;
  if (!Resolve(rel, abs, r, true)) return r;

  if (oldS.empty()) {
    r.output = "old_string пуст — такие правки запрещены. Используйте write_file для новых файлов.";
    return r;
  }
  std::string text;
  if (!platform::ReadTextFile(abs, text)) {
    r.output = "Файл не найден или не читается: " + rel;
    return r;
  }

  size_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(oldS, pos)) != std::string::npos) {
    ++count;
    pos += oldS.size();
  }
  if (count == 0) {
    r.output =
        "Фрагмент old_string НЕ найден в файле " + rel +
        ". Прочитайте файл (read_file) и пришлите ТОЧНЫЙ фрагмент, включая пробелы и переводы строк.";
    return r;
  }
  if (count > 1 && !replaceAll) {
    r.output = "Фрагмент встречается " + std::to_string(count) +
               " раз(а). Уточните контекст (сделайте old_string длиннее) "
               "или вызовите с replace_all=true.";
    return r;
  }

  if (mCtx.snapshots) mCtx.snapshots->RecordFileChange(abs);

  std::string result;
  result.reserve(text.size() + newS.size());
  size_t prev = 0;
  pos = 0;
  int replaced = 0;
  while ((pos = text.find(oldS, prev)) != std::string::npos) {
    result.append(text, prev, pos - prev);
    result.append(newS);
    prev = pos + oldS.size();
    ++replaced;
    if (!replaceAll) break;
  }
  result.append(text, prev, std::string::npos);

  std::string werr;
  if (!platform::WriteTextFile(abs, result, &werr)) {
    r.output = "Ошибка записи: " + werr;
    return r;
  }
  r.ok = true;
  r.output = "Файл " + rel + ": выполнено замен: " + std::to_string(replaced);
  if (mCtx.project) mCtx.project->MarkDirty();
  if (mCtx.fileChanged) mCtx.fileChanged(rel);
  if (mCtx.log) mCtx.log("tool", r.output);
  return r;
}

ToolRunResult ToolRegistry::MakeDir(const nlohmann::json& args) {
  ToolRunResult r;
  const std::string rel = Arg(args, "path");
  fs::path abs;
  if (!Resolve(rel, abs, r, true)) return r;

  std::error_code ec;
  fs::create_directories(abs, ec);
  if (ec) {
    r.output = "Не удалось создать папку " + rel + ": " + ec.message();
    return r;
  }
  r.ok = true;
  r.output = "Папка создана: " + rel;
  if (mCtx.project) mCtx.project->MarkDirty();
  if (mCtx.log) mCtx.log("tool", r.output);
  return r;
}

ToolRunResult ToolRegistry::DeletePath(const nlohmann::json& args) {
  ToolRunResult r;
  const std::string rel = Arg(args, "path");
  const bool recursive = args.value("recursive", false);
  fs::path abs;
  if (!Resolve(rel, abs, r, true)) return r;

  std::error_code ec;
  if (!fs::exists(abs, ec)) {
    r.output = "Путь не существует: " + rel;
    return r;
  }
  if (fs::is_directory(abs, ec)) {
    if (!recursive) {
      r.output = "Это папка. Для удаления укажите recursive=true.";
      return r;
    }
    // Сохраняем в снимок каждый файл папки.
    if (mCtx.snapshots)
      for (auto it = fs::recursive_directory_iterator(
               abs, fs::directory_options::skip_permission_denied, ec);
           !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) mCtx.snapshots->RecordFileChange(it->path());
    fs::remove_all(abs, ec);
  } else {
    if (mCtx.snapshots) mCtx.snapshots->RecordFileChange(abs);
    fs::remove(abs, ec);
  }
  if (ec) {
    r.output = "Ошибка удаления " + rel + ": " + ec.message();
    return r;
  }
  r.ok = true;
  r.output = "Удалено: " + rel + (recursive ? " (рекурсивно)" : "");
  if (mCtx.project) mCtx.project->MarkDirty();
  if (mCtx.fileChanged) mCtx.fileChanged(rel);
  if (mCtx.log) mCtx.log("tool", r.output);
  return r;
}

ToolRunResult ToolRegistry::SearchFiles(const nlohmann::json& args) {
  ToolRunResult r;
  const std::string query = Arg(args, "query");
  const bool caseSens = args.value("case_sensitive", false);
  if (query.empty()) {
    r.output = "Пустой запрос.";
    return r;
  }
  fs::path abs;
  if (!Resolve(Arg(args, "path", "."), abs, r, false)) return r;

  const std::string needle = caseSens ? query : Lower(query);
  std::ostringstream out;
  int matches = 0;
  const int kMaxMatches = 200;
  long long filesScanned = 0;

  std::error_code ec;
  if (fs::is_regular_file(abs, ec) && LooksText(platform::PathToStr(abs.filename()))) {
    // один файл
    std::string text;
    if (platform::ReadTextFile(abs, text)) {
      std::istringstream in(text);
      std::string line;
      int n = 0;
      while (std::getline(in, line) && matches < kMaxMatches) {
        ++n;
        std::string hay = caseSens ? line : Lower(line);
        if (hay.find(needle) != std::string::npos) {
          out << Arg(args, "path") << ":" << n << ": " << line << "\n";
          ++matches;
        }
      }
    }
  } else {
    for (auto it = fs::recursive_directory_iterator(
             abs, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator() && matches < kMaxMatches;
         it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      const fs::path& p = it->path();
      std::string fname = platform::PathToStr(p.filename());
      if (!LooksText(fname)) continue;
      if (it->file_size(ec) > 512 * 1024) continue;
      std::string rel = platform::PathToStr(fs::relative(p, mCtx.project->Root(), ec));
      std::replace(rel.begin(), rel.end(), '\\', '/');
      ++filesScanned;
      std::string text;
      if (!platform::ReadTextFile(p, text)) continue;
      std::istringstream in(text);
      std::string line;
      int n = 0;
      while (std::getline(in, line) && matches < kMaxMatches) {
        ++n;
        std::string hay = caseSens ? line : Lower(line);
        if (hay.find(needle) != std::string::npos) {
          if (!line.empty() && line.back() == '\r') line.pop_back();
          out << rel << ":" << n << ": " << line << "\n";
          ++matches;
        }
      }
    }
  }
  if (matches == 0) out << "Совпадений не найдено.";
  else out << "-- совпадений: " << matches << (matches >= kMaxMatches ? " (обрезано)" : "")
           << ", файлов просмотрено: " << filesScanned;
  r.ok = true;
  r.output = out.str();
  return r;
}

ToolRunResult ToolRegistry::RunCommand(const nlohmann::json& args) {
  ToolRunResult r;
  const std::string cmd = Arg(args, "command");
  if (cmd.empty()) {
    r.output = "Пустая команда.";
    return r;
  }
  if (!mCtx.allowShell || !mCtx.allowShell()) {
    r.output =
        "Выполнение команд запрещено пользователем. Скажите пользователю: включите опцию "
        "«Разрешить агенту консольные команды» в Настройках, если он вам доверяет.";
    if (mCtx.log) mCtx.log("warn", "run_command отклонён (shell выключен): " + cmd);
    return r;
  }
  if (!mCtx.project || !mCtx.project->IsOpen()) {
    r.output = "Проект не открыт.";
    return r;
  }
  int timeout = args.value("timeout_sec", mCtx.shellTimeoutSec ? mCtx.shellTimeoutSec() : 60);
  if (timeout <= 0) timeout = 60;
  if (timeout > 600) timeout = 600;

  if (mCtx.log) mCtx.log("cmd", "$ " + cmd);
  auto res = platform::RunCommandCapture(cmd, mCtx.project->Root(), timeout);

  std::ostringstream out;
  out << "$ " << cmd << "\n";
  out << res.output;
  if (res.timedOut) out << "\n[команда прервана по таймауту " << timeout << " с]";
  out << "\n[код выхода: " << res.exitCode << "]";
  r.ok = res.exitCode == 0 && !res.timedOut;
  r.output = out.str();
  if (mCtx.log)
    mCtx.log(r.ok ? "cmd" : "error",
             "  → код " + std::to_string(res.exitCode) + (res.timedOut ? " (таймаут)" : ""));
  if (mCtx.project) mCtx.project->MarkDirty();  // команда могла изменить файлы
  return r;
}
