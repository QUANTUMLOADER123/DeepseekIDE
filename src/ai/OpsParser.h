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

}  // namespace dside
