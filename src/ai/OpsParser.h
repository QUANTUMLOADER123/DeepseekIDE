#pragma once

#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

// Чистый парсер ответов веб-чата: выделяет блоки ```deepseekide-ops,
// разбирает JSON внутри и возвращает список операций + текст без блоков.
// Без зависимостей от GUI и ToolRegistry (используется и в selftest).
struct ParsedOps {
  std::string text;                    // ответ без блоков операций (обрезанный)
  std::vector<nlohmann::json> ops;     // [{"name": "...", "args": {...}}, ...]
  std::vector<std::string> errors;     // нераспарсенное/запрещённое (для показа)
};

namespace dside {

// known — допустимые имена операций (например, мутации ToolRegistry).
// Возвращает true, если найдена хотя бы одна допустимая операция.
bool ExtractOps(const std::string& answer, const std::set<std::string>& known, ParsedOps& out);

// Список операций-мутаций, разрешённых веб-агенту.
const std::set<std::string>& MutationOps();

// Mutations + run_command: что вообще разрешено веб-агенту ИСПОЛНЯТЬ через
// блоки deepseekide-ops (сам run_command внутри всё равно стоит на гейтинге
// настройки «Разрешить агенту консольные команды»).
const std::set<std::string>& ChatAllowedOps();

// Разбирает ТЕЛО одного блока deepseekide-ops (валидный JSON: объект {name,args}
// или массив таких) и складывает операции/ошибки в out. blockIndex — номер
// блока для текста ошибки («Блок №N»). Нужен, когда блоки приходят не из
// текстового забора ``` , а из DOM страницы (пре-код с баннером языка).
void ParseOpsBody(const std::string& body, const std::set<std::string>& known,
                  int blockIndex, ParsedOps& out);

// Извлекает запросы «NEED SEARCH: <текст>» / «НУЖЕН ПОИСК: <текст>» — те же
// строгие правила (строка начинается с маркера), валидация — непусто и ≤200.
std::vector<std::string> FindSearchRequests(const std::string& answer);

// Извлекает запросы «НУЖЕН ФАЙЛ: <путь>» из текста ответа (системный промпт
// учит модель так просить содержимое файла). Ищется только ВНЕ блоков
// ```кода``` (внутри write_file-контента могло встретиться случайное слово).
// Пути очищены от кавычек/бэктиков/пробелов, дубликаты убраны, не больше 5.
std::vector<std::string> FindFileRequests(const std::string& answer);

}  // namespace dside
