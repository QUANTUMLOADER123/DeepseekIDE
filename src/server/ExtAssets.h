#pragma once

#include <filesystem>
#include <string>

// Генерирует распакованное браузерное расширение DeepSeekIDE (MV3) в dir:
// manifest.json, sw.js (service worker — мост к локальному API без CORS),
// content.js (автопилот + виджет на странице chat.deepseek.com),
// config.js (порт+токен, затираемые при каждой установке).
// Возвращает false с текстом err, если не удалось записать.
bool WriteExtensionAssets(const std::filesystem::path& dir, int port, const std::string& token,
                          std::string& err);
