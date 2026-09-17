#pragma once

#include <string>

namespace dside {

// Системный промпт агента DeepSeekIDE (английский, autonomous senior engineer).
// tree — дерево проекта (результат list_files, уже UTF-8-санитизированное).
// task — задача пользователя; пустая → промпт-праймер для расширения
//        (модель коротко подтверждает бриф и ждёт первое сообщение человека).
std::string BuildAgentPrompt(const std::string& tree, const std::string& task);

}  // namespace dside
