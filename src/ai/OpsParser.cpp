#include "ai/OpsParser.h"

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

    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded()) {
      out.errors.push_back("Блок deepseekide-ops №" + std::to_string(blocks) +
                           ": нечитаемый JSON — операция пропущена");
    } else {
      std::vector<nlohmann::json> items;
      if (j.is_array()) {
        items.assign(j.begin(), j.end());
      } else if (j.is_object() && j.contains("operations") && j["operations"].is_array()) {
        items.assign(j["operations"].begin(), j["operations"].end());
      } else if (j.is_object() && j.contains("name")) {
        items.push_back(j);
      } else {
        out.errors.push_back("Блок deepseekide-ops №" + std::to_string(blocks) +
                             ": ожидался объект {\"name\", \"args\"} или массив таких объектов");
      }
      for (auto& item : items) {
        if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) {
          out.errors.push_back("Элемент без поля name в блоке №" + std::to_string(blocks));
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
    if (close == std::string::npos) break;
    pos = close + fence.size();
  }

  out.text = Trim(rest);
  return !out.ops.empty();
}
