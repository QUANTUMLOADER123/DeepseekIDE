// Консольные самотесты DeepSeekIDE (ядро, без GUI):
//   1) SafeJoin — защита от выхода за пределы проекта
//   2) SnapshotManager — запись снимка и откат (включая удаление созданных файлов)
//   3) JSON-схемы инструментов
//   4) (если есть сеть) реальный HTTP-запрос через наш HttpClient

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>

#include "ai/OpsParser.h"
#include "ai/ToolRegistry.h"
#include "util/TextStitch.h"
#include "util/Utf8.h"
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
// 4) Схемы инструментов
static void TestToolSchemas() {
  std::printf("[4] Tool schemas\n");
  ToolContext ctx;  // без проекта — schema всё равно строится
  ToolRegistry reg(std::move(ctx));
  const auto& s = reg.Schemas();
  CHECK(s.is_array() && s.size() == 13, "тринадцать инструментов");
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

// 5b) Новые операции агента: append/insert/replace/copy/move
static void TestNewToolOps(const fs::path& root) {
  std::printf("[5b] Новые операции инструментов\n");
  ProjectManager pm;
  pm.SetRoot(root);
  SnapshotManager snaps;
  snaps.SetRoot(pm.Root());
  ToolContext ctx;
  ctx.project = &pm;
  ctx.snapshots = &snaps;
  ToolRegistry reg(std::move(ctx));

  std::string file = "ops/lines.txt";
  auto w = reg.Execute("write_file", {{"path", file}, {"content", "one\ntwo\nthree\n"}});
  CHECK(w.ok, "write_file для построчных правок");

  auto ins = reg.Execute("insert_lines", {{"path", file}, {"line", 2}, {"content", "BEFORE-two"}});
  CHECK(ins.ok, "insert_lines вставляет перед строкой");
  auto rf = reg.Execute("read_file", {{"path", file}});
  CHECK(rf.output.find("BEFORE-two") != std::string::npos, "вставка на месте");
  CHECK(rf.output.find("one") < rf.output.find("BEFORE-two"), "порядок строк верный");

  auto bad = reg.Execute("insert_lines", {{"path", file}, {"line", 99}, {"content", "x"}});
  CHECK(!bad.ok, "insert_lines вне диапазона = отказ");

  auto rpl = reg.Execute("replace_lines",
                         {{"path", file}, {"start_line", 1}, {"end_line", 1}, {"content", "ONE"}});
  CHECK(rpl.ok, "replace_lines заменяет диапазон");
  auto rf2 = reg.Execute("read_file", {{"path", file}});
  CHECK(rf2.output.find("ONE") != std::string::npos && rf2.output.find("BEFORE-two") != std::string::npos,
        "замена одних строк не трогает соседние");

  auto app = reg.Execute("append_file", {{"path", file}, {"content", "tail"}});
  CHECK(app.ok, "append_file дописывает");
  auto rf3 = reg.Execute("read_file", {{"path", file}});
  CHECK(rf3.output.find("tail") != std::string::npos, "хвост записан");

  auto appNew = reg.Execute("append_file", {{"path", "ops/newfile.txt"}, {"content", "fresh\n"}});
  CHECK(appNew.ok && fs::exists(pm.Root() / "ops" / "newfile.txt"), "append_file создаёт новый файл");

  auto cp = reg.Execute("copy_file", {{"from", file}, {"to", "ops/copy.txt"}});
  CHECK(cp.ok && fs::exists(pm.Root() / "ops" / "copy.txt"), "copy_file копирует");
  auto cpBad = reg.Execute("copy_file", {{"from", file}, {"to", "ops/copy.txt"}});
  CHECK(!cpBad.ok, "copy_file не затирает существующую цель");

  auto mv = reg.Execute("move_file", {{"from", "ops/copy.txt"}, {"to", "ops/moved.txt"}});
  CHECK(mv.ok && fs::exists(pm.Root() / "ops" / "moved.txt") &&
            !fs::exists(pm.Root() / "ops" / "copy.txt"),
        "move_file перемещает");
}

// 7) Парсер ответов веб-чата (deepseekide-ops)
static void TestOpsParser() {
  std::printf("[7] Парсер deepseekide-ops\n");
  ParsedOps po;

  std::string answer =
      "Сделал! Пояснение до блока.\n"
      "```deepseekide-ops\n"
      "[{\"name\":\"write_file\",\"args\":{\"path\":\"a.txt\",\"content\":\"x\"}},"
      " {\"name\":\"make_dir\",\"args\":{\"path\":\"docs\"}}]\n"
      "```\n"
      "А это обычный код:\n```cpp\nint main(){}\n```\n"
      "И ещё одна операция:\n```deepseekide-ops\n"
      "{\"name\":\"append_file\",\"args\":{\"path\":\"a.txt\",\"content\":\"y\"}}\n```";
  bool any = dside::ExtractOps(answer, dside::MutationOps(), po);
  CHECK(any, "найдены операции");
  CHECK(po.ops.size() == 3, "ровно три операции (две в массиве + один объект)");
  CHECK(po.ops[0]["name"] == "write_file" && po.ops[2]["name"] == "append_file", "разбор корректный");
  CHECK(po.text.find("int main()") != std::string::npos, "обычный блок кода не вырезается");
  CHECK(po.text.find("write_file") == std::string::npos, "ops-блоки вырезаны из текста");
  CHECK(po.errors.empty(), "ошибок разбора нет");

  // Повреждённый JSON — ошибка, но не падение:
  ParsedOps p2;
  dside::ExtractOps("x\n```deepseekide-ops\n{ не json }\n```", dside::MutationOps(), p2);
  CHECK(p2.ops.empty() && p2.errors.size() == 1, "битый JSON → одна ошибка, без краха");

  // Незнакомая операция — в errors, не в ops:
  ParsedOps p3;
  dside::ExtractOps("```deepseekide-ops\n{\"name\":\"hack_all\",\"args\":{}}\n```",
                    dside::MutationOps(), p3);
  CHECK(p3.ops.empty() && !p3.errors.empty(), "незнакомая операция отклонена");

  // Блоки только чтения тоже не должны проходить как мутации:
  ParsedOps p4;
  dside::ExtractOps("```deepseekide-ops\n{\"name\":\"read_file\",\"args\":{\"path\":\"a\"}}\n```",
                    dside::MutationOps(), p4);
  CHECK(p4.ops.empty(), "read_file не входит в мутации");

  // «НУЖЕН ФАЙЛ: …» — контракт промпта: просьба содержимого файла
  {
    const auto r1 = dside::FindFileRequests("Текст.\nНУЖЕН ФАЙЛ: src/main.cpp\nЕщё текст");
    CHECK(r1.size() == 1 && r1[0] == "src/main.cpp", "простой запрос файла");
    const auto r2 = dside::FindFileRequests(
        "НУЖЕН ФАЙЛ: `site/css/style.css`.\nНУЖЕН ФАЙЛ: site/css/style.css\nНужен файл: script.js");
    CHECK(r2.size() == 2, "дубликат склеился, вариант «Нужен файл:» тоже ловится");
    CHECK(r2.size() == 2 && r2[0] == "site/css/style.css" && r2[1] == "script.js",
          "пути очищены от бэктиков и пунктуации");
    const auto r3 = dside::FindFileRequests("```cpp\n// НУЖЕН ФАЙЛ: x.h\n```\nГотово");
    CHECK(r3.empty(), "маркер внутри блока кода игнорируется");
  }
}


// 7) UTF-8 безопасность: санитизация, обрезка по границе символа, неубиваемый dump
static void TestUtf8Safety() {
  std::printf("[7] UTF-8: sanitize/truncate/dump\n");

  // Валидные строки не трогаем
  const std::string good = "Привет, мир — DeepSeekIDE 🎉";
  CHECK(utf8::Valid(good), "кириллица+эмодзи — валидный UTF-8");
  CHECK(utf8::Sanitize(good) == good, "валидная строка проходит без изменений");

  // Байт 0xE2 без продолжения (как у пользователя в логе) удаляется
  {
    std::string bad = "Нормальный текст";
    bad.push_back('\xE2');  // обрыв 3-байтной последовательности
    bad += " хвост";
    CHECK(!utf8::Valid(bad), "обрыв E2 — невалиден");
    const std::string fixed = utf8::Sanitize(bad);
    CHECK(utf8::Valid(fixed), "после Sanitize — валиден");
    CHECK(fixed == "Нормальный текст хвост", "убран только битый байт");
  }

  // Сиротные continuation-байты и обрывы эмодзи
  {
    std::string bad;
    bad.push_back('\xF0'); bad.push_back('\x9F');  // половина 🎉
    bad += "ok";
    const std::string fixed = utf8::Sanitize(bad);
    CHECK(fixed == "ok" && utf8::Valid(fixed), "полуэмодзи вычищен");
  }

  // Overlong и суррогаты бракуем
  {
    std::string overlong;
    overlong.push_back('\xC0'); overlong.push_back('\x80');  // '.' в overlong
    CHECK(!utf8::Valid(overlong), "overlong C0 80 — невалиден");
    std::string surr;
    surr.push_back('\xED'); surr.push_back('\xA0'); surr.push_back('\x80');  // U+D800
    CHECK(!utf8::Valid(surr), "закодированный суррогат — невалиден");
  }

  // Обрезка: 60-байтовый срез кириллической задачи не рвёт символ
  {
    const std::string task = "СОздай тут какой то дефолтный сайт (Тест)";  // 74 байта
    const std::string t = utf8::Truncate(task, 60);
    CHECK(t.size() <= 60 && utf8::Valid(t), "срез на границе символа");
    const std::string title = t + "…";
    CHECK(utf8::Valid(title), "заголовок с многоточием валиден");
  }

  // Обрезка короче строки, где байт maxBytes — lead-многобайтового
  {
    const std::string s = "абвгде";  // 12 байт
    const std::string t = utf8::Truncate(s, 5);  // режем посреди 3-й буквы 'в'
    CHECK(t == "аб" && utf8::Valid(t), "недособранный символ отброшен");
  }

  // Неубиваемый dump: битый UTF-8 внутри JSON не должен ронять dump()
  {
    nlohmann::json j;
    std::string bad = "abc";
    bad.push_back('\xE2');  // как в type_error.316 у пользователя
    bad += "def";
    j["text"] = bad;
    std::string out;
    bool threw = false;
    try {
      out = utf8::DumpJson(j, 1);
    } catch (const std::exception& e) {
      threw = true;
      std::printf("  [FAIL] DumpJson кинул: %s\n", e.what());
    }
    CHECK(!threw, "DumpJson не бросает на битом UTF-8");
    CHECK(out.find("\\uFFFD") != std::string::npos || out.find("�") != std::string::npos,
          "битый байт заменён на U+FFFD, а не крах");
    // строгий dump на той же строке обязан бросить — значит, регрессия реальна
    bool strictThrew = false;
    try { (void)j.dump(); } catch (const std::exception&) { strictThrew = true; }
    CHECK(strictThrew, "контроль: обычный dump() на таких данных действительно падает");
  }
}


// 8) Склейка окон ответа: JS отдаёт только последний кусок, накопитель полнотекста
static void TestTextStitch() {
  std::printf("[8] Склейка окон ответа\n");

  // Потоковый рост: два окна с перекрытием
  {
    std::string full;
    stitch::AppendWindow(full, "Начало ответа, пишем дальше…");
    stitch::AppendWindow(full, "пишем дальше… ещё кусок");
    CHECK(full == "Начало ответа, пишем дальше… ещё кусок",
          "перекрывающиеся окна склеены без дубля");
  }

  // Полное перекрытие (то же окно повторно) — не растём
  {
    std::string full = "одно и то же";
    stitch::AppendWindow(full, "одно и то же");
    CHECK(full == "одно и то же", "повтор окна не дублируется");
  }

  // Длинная генерация, посчитанная кусками по модулю окна 60000
  {
    std::string big;
    for (int i = 0; i < 200000; ++i) big.push_back(char('a' + ((unsigned)(i * 2654435761u) >> 24) % 26));
    const size_t win = 60000;
    std::string full;
    for (size_t shown = 1000; shown <= big.size(); shown += 1000) {
      const size_t start = shown > win ? shown - win : 0;
      stitch::AppendWindow(full, big.substr(start, shown - start));
    }
    CHECK(full == big, "200К-символьный ответ собран без потерь");
  }

  // Разрыв окна (между опросами прилетело больше, чем размер окна):
  // кусок дописывается целиком с маркером — текст начала НЕ затирается
  {
    std::string full = "ранний…текст";
    stitch::AppendWindow(full, "СОВСЕМ другой фрагмент");
    CHECK(full.find("ранний") != std::string::npos &&
              full.find("СОВСЕМ другой фрагмент") != std::string::npos,
          "окно без перекрытия дописано, а не подменило накопитель");
  }

  // Перекрытие по UTF-8 не портит строки
  {
    std::string full;
    stitch::AppendWindow(full, "Ура 🎉 сайт");
    stitch::AppendWindow(full, "сайт готов ✅");
    CHECK(utf8::Valid(full) && full.find("🎉") != std::string::npos,
          "многобайтовые на стыке не ломаются");
  }
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
  TestToolSchemas();
  TestToolsLive(root);
  TestNewToolOps(root);
  TestOpsParser();
  TestUtf8Safety();
  TestTextStitch();
  TestNetwork();

  std::error_code ec;
  fs::remove_all(root, ec);

  std::printf("=== Итог: %d passed, %d failed ===\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
