#pragma once

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

class ProjectManager;
class SnapshotManager;

// Контекст исполнения инструментов (всё живёт в Application).
struct ToolContext {
  ProjectManager* project = nullptr;
  SnapshotManager* snapshots = nullptr;

  // Лог в нижнюю панель (вызывается из потока агента — реализация должна быть потокобезопасной).
  std::function<void(const std::string& level, const std::string& msg)> log;

  // Уведомление об изменении файла (относительный путь) — обновить UI.
  std::function<void(const std::string& relPath)> fileChanged;

  // Разрешены ли консольные команды + таймаут.
  std::function<bool()> allowShell;
  std::function<int()> shellTimeoutSec;
};

struct ToolRunResult {
  bool ok = false;
  std::string output;  // уходит модели целиком (в разумных пределах) и в UI (обрезанный)
};

// Реализация инструментов агента (function calling DeepSeek).
class ToolRegistry {
public:
  explicit ToolRegistry(ToolContext ctx);



  // JSON-схемы в формате OpenAI tools (для поля "tools" запроса).
  const nlohmann::json& Schemas() const { return mSchemas; }

  // Выполнить вызов. Вызывать ТОЛЬКО из потока агента.
  ToolRunResult Execute(const std::string& name, const nlohmann::json& args);

  // Короткое описание вызова для UI ("write_file · src/main.cpp").
  static std::string Describe(const std::string& name, const nlohmann::json& args);

private:
  void BuildSchemas();
  ToolRunResult ListFiles(const nlohmann::json& args);
  ToolRunResult ReadFile(const nlohmann::json& args);
  ToolRunResult WriteFile(const nlohmann::json& args);
  ToolRunResult EditFile(const nlohmann::json& args);
  ToolRunResult AppendFile(const nlohmann::json& args);
  ToolRunResult InsertLines(const nlohmann::json& args);
  ToolRunResult ReplaceLines(const nlohmann::json& args);
  ToolRunResult MakeDir(const nlohmann::json& args);
  ToolRunResult DeletePath(const nlohmann::json& args);
  ToolRunResult CopyFile(const nlohmann::json& args);
  ToolRunResult MoveFile(const nlohmann::json& args);
  ToolRunResult SearchFiles(const nlohmann::json& args);
  ToolRunResult RunCommand(const nlohmann::json& args);

  // Общие помощники для построчных правок.
  static bool SplitLines(const std::string& text, std::vector<std::string>& lines);
  static std::string JoinLines(const std::vector<std::string>& lines);

  std::string Arg(const nlohmann::json& args, const char* key, const std::string& def = "") const;
  // Проверка пути: внутри проекта + не защищённый.
  bool Resolve(const std::string& rel, std::filesystem::path& out, ToolRunResult& err,
               bool forWrite) const;

  ToolContext mCtx;
  nlohmann::json mSchemas;
};
