#pragma once

#include <set>
#include <string>

class ToolRegistry;

namespace dside {

// Самообслуживание модели: вытаскивает из ответа директивы NEED FILE/NEED
// SEARCH и готовит системную заметку с содержимым файлов / результатами
// поиска. Поштучный дедуп: sentFiles/sentSearches — уже отправленное этому
// диалогу (сбрасываются владельцем на новую задачу/чат). Порции: не больше
// 3 файлов и 2 поисков за заметку; хвост модель добирает повторной директивой
// (текст заметки это оговаривает). logOut — короткое описание для журнала
// (пустое, если заметка не сформирована). Возвращает "" без изменений.
std::string CollectServiceNote(ToolRegistry* tools, const std::string& answerRaw,
                               std::set<std::string>& sentFiles,
                               std::set<std::string>& sentSearches, std::string& logOut);

}  // namespace dside
