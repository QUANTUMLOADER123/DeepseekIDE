#include "util/Utf8.h"

namespace {

// Сколько байтов занимает последовательность, начинающаяся с lead-байта b.
// Возвращает 0 для некорректного lead, отрицательное значение не используем.
int SeqLen(unsigned char b) {
  if (b < 0x80) return 1;
  if (b >= 0xC2 && b <= 0xDF) return 2;
  if (b >= 0xE0 && b <= 0xEF) return 3;
  if (b >= 0xF0 && b <= 0xF4) return 4;
  return 0;  // 0x80..0xBF сирота, 0xC0/0xC1 overlong, 0xF5..0xFF запрещены
}

bool IsCont(unsigned char b) { return (b & 0xC0) == 0x80; }

// Строгая проверка одной последовательности длиной n, начиная с позиции i.
// Помимо continuation-байтов проверяет overlong и суррогаты/диапазон.
bool SeqOk(const std::string& s, size_t i, int n) {
  const unsigned char b0 = static_cast<unsigned char>(s[i]);
  for (int k = 1; k < n; ++k) {
    if (i + static_cast<size_t>(k) >= s.size()) return false;  // обрыв в конце
    if (!IsCont(static_cast<unsigned char>(s[i + static_cast<size_t>(k)]))) return false;
  }
  switch (n) {
    case 1:
      return true;
    case 2:
      return true;  // диапазон lead 0xC2..0xDF уже отбрасывает overlong
    case 3: {
      const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
      if (b0 == 0xE0 && b1 < 0xA0) return false;                 // overlong
      if (b0 == 0xED && b1 >= 0xA0) return false;                // суррогаты D800..DFFF
      return true;
    }
    case 4: {
      const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
      if (b0 == 0xF0 && b1 < 0x90) return false;                 // overlong
      if (b0 == 0xF4 && b1 > 0x8F) return false;                 // > U+10FFFF
      return true;
    }
    default:
      return false;
  }
}

}  // namespace

bool utf8::Valid(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    const int n = SeqLen(static_cast<unsigned char>(s[i]));
    if (n == 0 || !SeqOk(s, i, n)) return false;
    i += static_cast<size_t>(n);
  }
  return true;
}

std::string utf8::Sanitize(const std::string& s) {
  // Быстрый путь: строка и так валидна (99% случаев) — копия как есть.
  if (Valid(s)) return s;
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const unsigned char b = static_cast<unsigned char>(s[i]);
    const int n = SeqLen(b);
    if (n > 0 && SeqOk(s, i, n)) {
      out.append(s, i, static_cast<size_t>(n));
      i += static_cast<size_t>(n);
    } else {
      ++i;  // битый байт — просто выбрасываем
    }
  }
  return out;
}

std::string utf8::Truncate(const std::string& s, size_t maxBytes) {
  if (s.size() <= maxBytes) return s;
  size_t cut = maxBytes;
  // Отступаем назад с continuation-байтов на начало символа…
  while (cut > 0 && IsCont(static_cast<unsigned char>(s[cut]))) --cut;
  // …а если срез пришёлся ровно на середину (lead остался последним),
  // отбрасываем и его — проще всего прогнать через Sanitize.
  return Sanitize(s.substr(0, cut));
}

std::string utf8::DumpJson(const nlohmann::json& j, int indent) {
  // error_handler_t::replace: вместо type_error.316 битые байты → U+FFFD.
  return j.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}
