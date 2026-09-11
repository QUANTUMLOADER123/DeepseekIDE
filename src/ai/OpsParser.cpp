#include "ai/OpsParser.h"

#include "util/Utf8.h"

#include <algorithm>
#include <cctype>

namespace {

std::string Trim(std::string s) {
  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
  return s;
}

std::string LowerAscii(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

const std::set<std::string>& dside::MutationOps() {
  static const std::set<std::string> ops = {"write_file", "edit_file",   "append_file",
                                            "insert_lines", "replace_lines", "make_dir",
                                            "delete_path", "copy_file",  "move_file"};
  return ops;
}

namespace {

// Вырезает первый сбалансированный JSON-объект/массив: находим первый '{' или '[',
// дальше идём со счётчиком глубины, отслеживая строки и экранирование (скобки
// ВНУТРИ строковых значений глубину не меняют). Виджет кодового блока на странице
// обкладывает JSON мусором (кнопки «Копировать»/«Скачать» перед ним и финальная
// проза ответа после) — строгий parse всего тела на этом падал с «нечитаемый JSON».
std::string ExtractJsonSpan(const std::string& s) {
  const size_t i = s.find_first_of("{[");
  if (i == std::string::npos) return {};
  int depth = 0;
  bool inStr = false, esc = false;
  for (size_t j = i; j < s.size(); ++j) {
    const char c = s[j];
    if (esc) { esc = false; continue; }
    if (inStr) {
      if (c == '\\') esc = true;
      else if (c == '"') inStr = false;
      continue;
    }
    if (c == '"') { inStr = true; continue; }
    if (c == '{' || c == '[') {
      ++depth;
    } else if (c == '}' || c == ']') {
      if (--depth == 0) return s.substr(i, j - i + 1);
      if (depth < 0) break;
    }
  }
  return s.substr(i);  // незакрытое — отдадим парсеру как есть, получим его ошибку
}

}  // namespace

void dside::ParseOpsBody(const std::string& body, const std::set<std::string>& known,
                         int blockIndex, ParsedOps& out) {
  auto j = nlohmann::json::parse(body, nullptr, false);
  if (j.is_discarded()) {
    // Мусор вокруг JSON («Копировать», «Скачать», финальная проза): режем до
    // сбалансированного спана и пробуем ещё раз.
    const std::string span = ExtractJsonSpan(body);
    if (!span.empty()) j = nlohmann::json::parse(span, nullptr, false);
  }
  if (j.is_discarded()) {
    // В лог — обрезанный превью тела (по границе символа), чтобы следующий
    // подобный репорт диагностировался с первого взгляда.
    const std::string preview = utf8::Truncate(utf8::Sanitize(Trim(body)), 160);
    out.errors.push_back("Блок deepseekide-ops №" + std::to_string(blockIndex) +
                         ": нечитаемый JSON — операция пропущена. Начало тела: «" +
                         preview + "»");
    return;
  }
  std::vector<nlohmann::json> items;
  if (j.is_array()) {
    items.assign(j.begin(), j.end());
  } else if (j.is_object() && j.contains("operations") && j["operations"].is_array()) {
    items.assign(j["operations"].begin(), j["operations"].end());
  } else if (j.is_object() && j.contains("name")) {
    items.push_back(j);
  } else {
    out.errors.push_back("Блок deepseekide-ops №" + std::to_string(blockIndex) +
                         ": ожидался объект {\"name\", \"args\"} или массив таких объектов");
  }
  for (auto& item : items) {
    if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) {
      out.errors.push_back("Элемент без поля name в блоке №" + std::to_string(blockIndex));
      continue;
    }
    const std::string name = item["name"].get<std::string>();
    if (known.count(name) == 0) {
      out.errors.push_back("Неизвестная или запрещённая операция: " + name);
      continue;
    }
    nlohmann::json op = {{"name", name}, {"args", nlohmann::json::object()}};
    if (item.contains("args") && item["args"].is_object()) op["args"] = item["args"];
    out.ops.push_back(std::move(op));
  }
}

bool dside::ExtractOps(const std::string& answer, const std::set<std::string>& known,
                       ParsedOps& out) {
  out = ParsedOps{};
  const std::string fence = "```";
  const std::string tag = "deepseekide-ops";
  std::string rest;
  rest.reserve(answer.size());

  size_t pos = 0;
  int blocks = 0;
  while (true) {
    size_t open = answer.find(fence, pos);
    if (open == std::string::npos) {
      rest.append(answer, pos, std::string::npos);
      break;
    }
    size_t tagStart = open + fence.size();
    size_t eol = answer.find('\n', tagStart);
    if (eol == std::string::npos) eol = answer.size();
    std::string firstWord = LowerAscii(Trim(answer.substr(tagStart, eol - tagStart)));
    if (firstWord != tag) {
      // Обычный блок кода — переносим как есть.
      size_t close = answer.find(fence, eol);
      size_t end = (close == std::string::npos) ? answer.size() : close + fence.size();
      rest.append(answer, pos, end - pos);
      pos = end;
      continue;
    }
    size_t close = answer.find(fence, eol);
    std::string body = (close == std::string::npos)
                           ? answer.substr(eol + 1)
                           : answer.substr(eol + 1, close - eol - 1);
    ++blocks;
    rest.append(answer, pos, open - pos);
    ParseOpsBody(body, known, blocks, out);
    if (close == std::string::npos) break;
    pos = close + fence.size();
  }

  out.text = Trim(rest);
  return !out.ops.empty();
}

namespace {

// Разрешённые написания маркера (модель обычно повторяет регистр промпта,
// но страхуемся от «нормального» письма).
const char* kMarkers[] = {"НУЖЕН ФАЙЛ:", "Нужен файл:", "нужен файл:"};

std::string CleanPath(std::string p) {
  auto junk = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '`' || c == '"' ||
           c == '\''  || c == '<' || c == '>';
  };
  // Крутим до стабилизации: «`path`.» → точка не junk, затем за ней бэктик.
  for (;;) {
    size_t before = p.size();
    while (!p.empty() && junk(p.front())) p.erase(p.begin());
    while (!p.empty() && junk(p.back())) p.pop_back();
    // Точка/запятая в конце — это пунктуация предложения, не часть пути
    while (!p.empty() && (p.back() == '.' || p.back() == ',')) p.pop_back();
    if (p.size() == before) break;
  }
  if (p.size() > 200) p.resize(200);
  return p;
}

}  // namespace

std::vector<std::string> dside::FindFileRequests(const std::string& answer) {
  std::vector<std::string> out;
  std::string line;
  bool inFence = false;
  for (size_t i = 0; i <= answer.size(); ++i) {
    if (i == answer.size() || answer[i] == '\n') {
      // Маркер ищем только вне блоков кода — внутри контента write_file
      // такая строка может встретиться как данные, а не просьба.
      if (line.rfind("```", 0) == 0) {
        inFence = !inFence;
      } else if (!inFence && out.size() < 5) {
        for (const char* m : kMarkers) {
          const std::string marker = m;
          const size_t at = line.find(marker);
          if (at != std::string::npos) {
            std::string path = CleanPath(line.substr(at + marker.size()));
            if (!path.empty()) {
              bool dup = false;
              for (const auto& e : out)
                if (e == path) { dup = true; break; }
              if (!dup) out.push_back(std::move(path));
            }
            break;
          }
        }
      }
      line.clear();
    } else {
      line.push_back(answer[i]);
    }
  }
  return out;
}
