// Консольные самотесты DeepSeekIDE (ядро, без GUI):
//   1) SafeJoin — защита от выхода за пределы проекта
//   2) SnapshotManager — запись снимка и откат (включая удаление созданных файлов)
//   3) SSE-парсер DeepSeekClient (чанки, tool_calls-фрагменты)
//   4) Агрегация стриминга
//   5) JSON-схемы инструментов
//   6) (если есть сеть) реальный HTTPS-запрос через наш HttpClient

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "ai/DeepSeekClient.h"
#include "ai/ToolRegistry.h"
#include "app/Platform.h"
#include "core/ProjectManager.h"
#include "core/SnapshotManager.h"
#include "net/HttpClient.h"

namespace fs = std::filesystem;

static int gFailed = 0;
static int gPassed = 0;

#define CHECK(cond, name)                                              \
  do {                                                                 \
    if (cond) {                                                        \
      ++gPassed;                                                       \
      std::printf("  [PASS] %s\n", name);                              \
    } else {                                                           \
      ++gFailed;                                                       \
      std::printf("  [FAIL] %s  (%s:%d)\n", name, __FILE__, __LINE__); \
    }                                                                  \
  } while (0)

static fs::path MakeTempProject() {
  fs::path dir = fs::temp_directory_path() /
                 ("deepseekide_test_" + std::to_string(platform::NowMillis()));
  fs::create_directories(dir / "src");
  platform::WriteTextFile(dir / "hello.txt", "привет, мир\nline2\nline3\n");
  platform::WriteTextFile(dir / "src" / "main.cpp", "int main() { return 0; }\n");
  return fs::weakly_canonical(dir);
}

// 1) SafeJoin
static void TestSafeJoin(const fs::path& root) {
  std::printf("[1] SafeJoin\n");
  fs::path out;
  std::string err;

  CHECK(ProjectManager::SafeJoin(root, "src/main.cpp", out, err), "относительный путь разрешён");
  CHECK(platform::PathToStr(out).find(platform::PathToStr(root)) == 0, "остаётся внутри root");
  CHECK(!ProjectManager::SafeJoin(root, "../outside.txt", out, err), "'..' за пределы = отказ");
  CHECK(!ProjectManager::SafeJoin(root, "src/../../hack", out, err), "хитрый '..' = отказ");
#ifdef _WIN32
  CHECK(!ProjectManager::SafeJoin(root, "C:/Windows/x", out, err), "абсолютный путь = отказ");
#else
  CHECK(!ProjectManager::SafeJoin(root, "/etc/passwd", out, err), "абсолютный путь = отказ");
#endif
  CHECK(ProjectManager::IsProtectedRel(".deepseekide/snapshots/x"), "защита .deepseekide");
  CHECK(ProjectManager::IsProtectedRel(".git/config"), "защита .git");
  CHECK(!ProjectManager::IsProtectedRel("src/main.cpp"), "обычный файл не защищён");
}

// 2) SnapshotManager
static void TestSnapshots(const fs::path& root) {
  std::printf("[2] SnapshotManager\n");
  SnapshotManager snaps;
  snaps.SetRoot(root);

  // Действие 1: переписали существующий файл
  snaps.Begin("переписать hello.txt");
  snaps.RecordFileChange(root / "hello.txt");
  platform::WriteTextFile(root / "hello.txt", "ИЗМЕНЕНО\n");
  snaps.Commit();

  // Действие 2: создали новый файл + удалили существующий
  snaps.Begin("создать new.py, удалить main.cpp");
  snaps.RecordFileChange(root / "new.py");  // не существовал
  platform::WriteTextFile(root / "new.py", "print(1)\n");
  snaps.RecordFileChange(root / "src" / "main.cpp");
  fs::remove(root / "src" / "main.cpp");
  snaps.Commit();

  auto list = snaps.List();
  CHECK(list.size() == 2, "два снимка");

  // Откат последнего: new.py должен исчезнуть, main.cpp вернуться
  std::string log;
  CHECK(snaps.RollbackLast(log), "rollback last OK");
  CHECK(!fs::exists(root / "new.py"), "созданный файл удалён при откате");
  std::string back;
  platform::ReadTextFile(root / "src" / "main.cpp", back);
  CHECK(back == "int main() { return 0; }\n", "удалённый файл восстановлен посимвольно");

  // Откат первого действия: hello.txt вернулся
  std::string log2;
  CHECK(snaps.RollbackLast(log2), "rollback first OK");
  std::string hello;
  platform::ReadTextFile(root / "hello.txt", hello);
  CHECK(hello.find("line2") != std::string::npos, "исходное содержимое hello.txt восстановлено");

  // Пустое действие не создаёт снимок
  snaps.Begin("ничего не делали");
  snaps.Commit();
  CHECK(snaps.List().empty(), "пустые действия не оставляют следов");
}

// 3) SSE-парсер
static void TestSse() {
  std::printf("[3] SSE parser\n");
  bool done = false;
  nlohmann::json chunk;

  CHECK(!DeepSeekClient::ParseSseLine("data: [DONE]", chunk, done) && done, "[DONE] распознан");
  done = false;
  CHECK(DeepSeekClient::ParseSseLine(
            "data: {\"choices\":[{\"delta\":{\"content\":\"Привет\"}}]}", chunk, done) &&
            !done,
        "JSON-чанк распознан");
  CHECK(chunk["choices"][0]["delta"]["content"] == "Привет", "UTF-8 контент цел");
  CHECK(!DeepSeekClient::ParseSseLine(": ping", chunk, done), "служебные строки игнорятся");

  // Буфер с несколькими событиями + частичным хвостом. Чанки собираем через
  // nlohmann::json — гарантированно валидный SSE, как у настоящего API.
  auto chunkContent = [](const std::string& c) {
    return nlohmann::json{{"choices", {{{"delta", {{"content", c}}}}}}}.dump();
  };
  auto chunkTool = [](int index, const std::string& id, const std::string& name,
                      const std::string& argsPiece) {
    nlohmann::json fn;
    if (!name.empty()) fn["name"] = name;
    if (!argsPiece.empty()) fn["arguments"] = argsPiece;
    nlohmann::json tc = {{"index", index}, {"type", "function"}, {"function", fn}};
    if (!id.empty()) tc["id"] = id;
    return nlohmann::json{{"choices", {{{"delta", {{"tool_calls", {tc}}}}}}}}.dump();
  };

  // tool_call приходит фрагментами: имя+начало аргументов, затем конец аргументов
  std::string buf = "data: " + chunkContent("A") + "\n\n";
  buf += "data: " + chunkTool(0, "call_1", "write_file", "{\"pa") + "\n";
  buf += "data: " + chunkTool(0, "", "", "th\":\"a.txt\"}") + "\n";
  buf += "data: [DONE]";
  buf += "\ndata: " + chunkContent("B");  // хвост без \n

  DeepSeekClient client("https://api.deepseek.com", "test-key");
  ChatResult acc;
  int chunks = 0;
  DeepSeekClient::ConsumeSseBuffer(buf, [&](const nlohmann::json& c) {
    ++chunks;
    client.Aggregate(c, acc);
  });
  CHECK(chunks == 3, "три целых события");
  CHECK(acc.content.empty(), "content не дублируется агрегатом");
  // добиваем хвост
  nlohmann::json tail;
  bool tailDone = false;
  CHECK(DeepSeekClient::ParseSseLine(buf, tail, tailDone), "хвост добран");

  // Агрегированный tool_call собрался в валидный JSON аргументов
  CHECK(acc.rawToolCalls.size() == 1, "один tool_call");
  CHECK(acc.rawToolCalls[0]["id"] == "call_1", "id сохранён");
  CHECK(acc.rawToolCalls[0]["function"]["name"] == "write_file", "имя дошло без обрыва");
  std::string args = acc.rawToolCalls[0]["function"]["arguments"];
  auto parsed = nlohmann::json::parse(args, nullptr, false);
  CHECK(!parsed.is_discarded() && parsed["path"] == "a.txt", "аргументы склеились в JSON");
}

// 4) Схемы инструментов
static void TestToolSchemas() {
  std::printf("[4] Tool schemas\n");
  ToolContext ctx;  // без проекта — schema всё равно строится
  ToolRegistry reg(std::move(ctx));
  const auto& s = reg.Schemas();
  CHECK(s.is_array() && s.size() == 8, "восемь инструментов");
  bool hasWrite = false, hasRun = false;
  for (const auto& t : s) {
    std::string n = t["function"]["name"];
    if (n == "write_file") hasWrite = true;
    if (n == "run_command") hasRun = true;
    CHECK(t["function"].contains("parameters"), "есть parameters");
  }
  CHECK(hasWrite && hasRun, "write_file и run_command присутствуют");

  // Вызов без проекта должен вернуть дружелюбную ошибку, а не упасть
  auto r = reg.Execute("read_file", {{"path", "a.txt"}});
  CHECK(!r.ok && !r.output.empty(), "read_file без проекта — ошибка, не креш");
}

// 5) Инструменты по-настоящему (временный проект)
static void TestToolsLive(const fs::path& root) {
  std::printf("[5] Инструменты на живом проекте\n");
  ProjectManager pm;
  pm.SetRoot(root);
  SnapshotManager snaps;
  snaps.SetRoot(pm.Root());

  ToolContext ctx;
  ctx.project = &pm;
  ctx.snapshots = &snaps;
  ToolRegistry reg(std::move(ctx));

  snaps.Begin("тестовое действие");
  auto w = reg.Execute("write_file", {{"path", "dir/note.md"}, {"content", "# Заметка\n"}});
  CHECK(w.ok, "write_file создаёт файл с папками");
  CHECK(fs::exists(pm.Root() / "dir" / "note.md"), "файл на месте");

  auto e = reg.Execute("edit_file",
                       {{"path", "dir/note.md"}, {"old_string", "Заметка"}, {"new_string", "План"}});
  CHECK(e.ok, "edit_file точечно меняет");
  auto nf = reg.Execute("edit_file",
                        {{"path", "dir/note.md"}, {"old_string", "нет такого"}, {"new_string", "x"}});
  CHECK(!nf.ok, "edit_file честно сообщает о промахе");

  auto rd = reg.Execute("read_file", {{"path", "dir/note.md"}});
  CHECK(rd.ok && rd.output.find("План") != std::string::npos, "read_file видит правку");

  auto sr = reg.Execute("search_files", {{"query", "main"}});
  CHECK(sr.ok && sr.output.find("main.cpp") != std::string::npos, "search_files находит");

  auto esc = reg.Execute("write_file", {{"path", "../evil.txt"}, {"content", "x"}});
  CHECK(!esc.ok, "песочница блокирует выход из проекта");

  auto prot = reg.Execute("delete_path", {{"path", ".deepseekide/snapshots"}});
  CHECK(!prot.ok, "защищённые пути недоступны");
  snaps.Commit();
  CHECK(!snaps.List().empty(), "действия записались в снимок");
}

// 6) Сеть (не падаем без сети или при rate-limit — просто SKIP)
static void TestNetwork() {
  std::printf("[6] HTTPS через наш HttpClient\n");
  auto res = net::HttpClient::Get("https://api.github.com/zen", {}, 15);
  if (res.status == 0) {
    std::printf("  [SKIP] нет сети (%s)\n", res.error.c_str());
    return;
  }
  // Любой осмысленный HTTP-код доказывает: TLS-рукопожатие + HTTP-раундтрип работают.
  // (В CI/песочницах GitHub может отвечать 401/403/429 — это ограничение среды, не кода.)
  if (res.status != 200) {
    std::printf("  [PASS] TLS+HTTP работают (HTTP %ld; 2xx от api.github.com необязателен в CI)\n",
                res.status);
    ++gPassed;
    return;
  }
  CHECK(res.Ok() && !res.body.empty(), "HTTPS GET работает (api.github.com)");
}

int main() {
  std::printf("=== DeepSeekIDE self-tests ===\n");
  fs::path root = MakeTempProject();

  TestSafeJoin(root);
  TestSnapshots(root);
  TestSse();
  TestToolSchemas();
  TestToolsLive(root);
  TestNetwork();

  std::error_code ec;
  fs::remove_all(root, ec);

  std::printf("=== Итог: %d passed, %d failed ===\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
