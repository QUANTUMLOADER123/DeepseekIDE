#pragma once

#include <functional>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "ai/OpsParser.h"
#include "ai/ToolRegistry.h"

// Ядро применения операций веб-агента: исполняет ops из JSON-массива через
// ToolRegistry, валидирует имена по белому списку чата (дефолт —
// dside::ChatAllowedOps(): мутации + run_command).
//
// В report — человекочитаемый построчный отчёт («✓ create …» / «✗ … — причина»)
// + итоговая строка «Готово: N из M операций успешно.».
// В runNote — вывод всех run_command (для автосообщения модели).
// Вызывать опционально snapBegin перед циклом и snapCommit после — они
// замыкают сессию снимков владельца.
// diag(name, r) — отладочный наблюдатель за исполнением каждой операции.
//
// Возвращает число успешно выполненных операций.
int ApplyChatOps(const nlohmann::json& ops, ToolRegistry& tools, std::string& report,
                 std::string& runNote, const std::function<void()>& snapBegin,
                 const std::function<void()>& snapCommit,
                 const std::set<std::string>* allowed = nullptr,
                 const std::function<void(const std::string& name, const ToolRunResult& r)>& diag =
                     nullptr);
