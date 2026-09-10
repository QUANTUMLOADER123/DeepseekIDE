#include "util/TextStitch.h"

namespace {

// Наибольшая длина k такая, что суффикс a длиной k совпадает с префиксом b.
// NB: свойства «если k валидно, то и k-1 валидно» НЕТ (бордеры строки),
// поэтому бинарный/геометрический поиск неприменим. Вместо него — якорь:
// в типовом стриминге перекрытие большое, так что берём хвост накопителя
// (64 байта) и ищем его вхождения в окне С КОНЦА: кандидат k = поз + 64,
// первый валидный сверху и есть максимум. Крошечные перекрытия (< 64)
// добираем коротким линейным проходом.
size_t MaxOverlapH(const std::string& a, const std::string& b) {
  size_t cap = a.size() < b.size() ? a.size() : b.size();
  // Прогресс обязателен: дописываем хотя бы один байт — иначе AppendWindow
  // навсегда застрянет на самоподобном (периодическом) тексте.
  if (cap == b.size() && cap > 0) --cap;
  if (cap == 0) return 0;

  auto suffixEq = [&](size_t k) {
    return k <= cap &&
           0 == std::char_traits<char>::compare(a.data() + a.size() - k, b.data(), k);
  };

  // Быстрый типовой случай: окно почти полностью повторяет конец накопителя
  // (между опросами дописалось мало символов). Правый край cap НЕ берём:
  // k == b.size() не дал бы прогресса и на самоподобном тексте (периодика)
  // навсегда залипал бы в «полном покрытии». Случай-дубликат разруливает
  // вызывающий через полное сравнение хвоста.
  if (cap < b.size() && suffixEq(cap)) return cap;

  const size_t anchorLen = cap < 64 ? cap : 64;
  const std::string anchor = a.substr(a.size() - anchorLen);
  size_t pos = b.rfind(anchor);
  while (pos != std::string::npos) {
    const size_t k = pos + anchorLen;
    if (suffixEq(k)) return k;
    if (pos == 0) break;
    pos = b.rfind(anchor, pos - 1);
  }
  for (size_t k = cap < 63 ? cap : 63; k > 0; --k)
    if (suffixEq(k)) return k;
  return 0;
}

}  // namespace

void stitch::AppendWindow(std::string& full, const std::string& window, size_t maxBytes) {
  if (window.empty()) return;
  if (full.empty()) {
    full = window;
    return;
  }
  // Дубликат-повтор (страница не изменилась между опросами) — выходим.
  if (window.size() <= full.size() &&
      0 == std::char_traits<char>::compare(full.data() + full.size() - window.size(),
                                           window.data(), window.size())) {
    return;
  }
  const size_t k = MaxOverlapH(full, window);
  if (k > 0) {
    full.append(window, k, std::string::npos);
  } else {
    // Стык потерян (между опросами выросло больше, чем размер окна) —
    // клеим внахлёст с маркером: ExtractOps всё равно разберёт блоки из обеих
    // частей, а в логе/сессии будет видно, что тут был скачок.
    full += "\n…\n";
    full += window;
  }
  if (full.size() > maxBytes) full.erase(0, full.size() - maxBytes);
}
