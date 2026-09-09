#include "ui/ChatPanel.h"

#include "misc/cpp/imgui_stdlib.h"

#include "app/Platform.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

void ChatPanel::AddUserMessage(const std::string& text) {
  ChatEntry e;
  e.kind = ChatEntry::Kind::User;
  e.title = platform::NowHMS();
  e.text = text;
  mEntries.push_back(std::move(e));
  mScrollToBottom = true;
}

void ChatPanel::AddInfo(const std::string& text) {
  ChatEntry e;
  e.kind = ChatEntry::Kind::Info;
  e.title = platform::NowHMS();
  e.text = text;
  mEntries.push_back(std::move(e));
  mScrollToBottom = true;
}

void ChatPanel::AddError(const std::string& text) {
  ChatEntry e;
  e.kind = ChatEntry::Kind::Error;
  e.title = platform::NowHMS();
  e.text = text;
  e.ok = false;
  mEntries.push_back(std::move(e));
  mScrollToBottom = true;
}

void ChatPanel::AddToolCall(const std::string& describe) {
  ChatEntry e;
  e.kind = ChatEntry::Kind::Tool;
  e.title = describe;
  mEntries.push_back(std::move(e));
  mScrollToBottom = true;
}

void ChatPanel::AppendToolResult(const std::string& text, bool ok) {
  // Приклеиваем к последней tool-записи
  for (auto it = mEntries.rbegin(); it != mEntries.rend(); ++it) {
    if (it->kind == ChatEntry::Kind::Tool && it->text.empty()) {
      it->text = text;
      it->ok = ok;
      mScrollToBottom = true;
      return;
    }
  }
  ChatEntry e;
  e.kind = ChatEntry::Kind::Tool;
  e.title = "результат";
  e.text = text;
  e.ok = ok;
  mEntries.push_back(std::move(e));
  mScrollToBottom = true;
}

void ChatPanel::BeginStream() {
  ChatEntry e;
  e.kind = ChatEntry::Kind::Assistant;
  e.title = platform::NowHMS();
  e.streaming = true;
  mEntries.push_back(std::move(e));
  mStreamIndex = (int)mEntries.size() - 1;
  mScrollToBottom = true;
}

void ChatPanel::AppendStreamDelta(const std::string& delta) {
  if (mStreamIndex >= 0 && mStreamIndex < (int)mEntries.size()) {
    mEntries[mStreamIndex].text += delta;
    mScrollToBottom = true;
  }
}

void ChatPanel::EndStream() {
  if (mStreamIndex >= 0 && mStreamIndex < (int)mEntries.size()) {
    mEntries[mStreamIndex].streaming = false;
    // Пустое сообщение (например, после серии только tool-вызовов) — убираем
    if (mEntries[mStreamIndex].text.empty()) mEntries.erase(mEntries.begin() + mStreamIndex);
  }
  mStreamIndex = -1;
  mScrollToBottom = true;
}

void ChatPanel::Clear() {
  mEntries.clear();
  mStreamIndex = -1;
}

void ChatPanel::Render() {
  if (ImGui::Begin("DeepSeek Ассистент")) {
    // ---------- Шапка ----------
    bool busy = mCb.busy && mCb.busy();
    if (busy) {
      widgets::Spinner("##agent_spin", 7.0f, 2.5f);
      ImGui::SameLine();
    }
    ImGui::AlignTextToFramePadding();
    std::string model = mCb.modelName ? mCb.modelName() : "deepseek-chat";
    ImGui::TextDisabled("%s", model.c_str());
    ImGui::SameLine();
    widgets::RightSide(96, [&] {
      if (ImGui::SmallButton("Настройки")) mCb.openSettings();
    });
    ImGui::SameLine();

    // Кнопка веб-чата chat.deepseek.com
    if (widgets::AccentButton("◆ chat.deepseek.com", ImVec2(0, 0)))
      mCb.openWebChat();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Открыть веб-чат DeepSeek в отдельном окне (логин через свой аккаунт)");
    ImGui::Separator();

    // ---------- Предупреждения ----------
    if (mCb.hasApiKey && !mCb.hasApiKey()) {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.25f, 0.05f, 0.55f));
      ImGui::BeginChild("##key_banner", ImVec2(0, 58), true);
      ImGui::TextWrapped("⚠ Нет API-ключа — агент недоступен. Укажите ключ в Настройках или пользуйтесь веб-чатом.");
      if (ImGui::SmallButton("Открыть настройки")) mCb.openSettings();
      ImGui::EndChild();
      ImGui::PopStyleColor();
    }
    if (mCb.hasProject && !mCb.hasProject()) {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
      ImGui::BeginChild("##proj_banner", ImVec2(0, 40), true);
      ImGui::TextWrapped("● Папка проекта не открыта — агент работает как простой чат, файлы недоступны.");
      ImGui::EndChild();
      ImGui::PopStyleColor();
    }

    // ---------- Лента ----------
    const float inputH = ImGui::GetFrameHeightWithSpacing() * 3.4f + 8.0f;
    ImGui::BeginChild("##chat_scroll", ImVec2(0, -inputH), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (mEntries.empty()) {
      ImGui::Dummy(ImVec2(0, 18));
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped(
          "Напишите, что сделать: например\n"
          "«создай hello world на Python»,\n"
          "«добавь README с описанием проекта»,\n"
          "«найди в проекте все TODO и выпиши списком».\n\n"
          "Агент сам изучит файлы, внесёт правки,\n"
          "а вы сможете откатить любое действие\n"
          "в панели «Снимки» ниже.");
      ImGui::PopStyleColor();
    }
    for (const auto& e : mEntries) RenderEntry(e);
    if (busy && mStreamIndex < 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::Colors().textDim);
      ImGui::TextUnformatted("DeepSeek думает…");
      ImGui::PopStyleColor();
    }
    if (mScrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) {
      ImGui::SetScrollHereY(1.0f);
      mScrollToBottom = false;
    }
    ImGui::EndChild();

    ImGui::Separator();

    // ---------- Поле ввода ----------
    ImGui::PushItemWidth(-1);
    bool sendPressed = ImGui::InputTextMultiline(
        "##chat_input", &mInput, ImVec2(-1, ImGui::GetTextLineHeight() * 3.4f),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Enter — отправить · Ctrl+Enter — новая строка");

    bool canSend = !busy && !mInput.empty();
    if (busy) ImGui::BeginDisabled();
    if (widgets::AccentButton("Отправить ▸") || (sendPressed && canSend)) {
      if (canSend && mCb.send) mCb.send(mInput);
      mInput.clear();
    }
    if (busy) ImGui::EndDisabled();
    ImGui::SameLine();
    if (busy) {
      if (ImGui::Button("■ Стоп")) mCb.stop();
      ImGui::SameLine();
    }
    if (ImGui::Button("Очистить")) mCb.clear();
    ImGui::SameLine();
    widgets::RightSide(150, [&] {
      if (ImGui::SmallButton("↺ Откатить посл.")) mCb.rollbackLast();
    });
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Откатить последнее действие агента (восстановить файлы из снимка)");
  }
  ImGui::End();
}

void ChatPanel::RenderEntry(const ChatEntry& e) {
  const auto& C = theme::Colors();
  const float width = ImGui::GetContentRegionAvail().x;

  switch (e.kind) {
    case ChatEntry::Kind::User: {
      // Пузырь справа
      float bubbleW = width * 0.86f;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - bubbleW);
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(C.userBubble));
      ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(C.userBubbleBorder));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
      ImGui::BeginChild((ImGuiID)(uintptr_t)&e, ImVec2(bubbleW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
      ImGui::PushTextWrapPos(bubbleW - 12.0f);
      RenderRichText(e.text);
      ImGui::PopTextWrapPos();
      ImGui::TextDisabled("%s · вы", e.title.c_str());
      ImGui::EndChild();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(2);
      break;
    }
    case ChatEntry::Kind::Assistant: {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(C.aiBubble));
      ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(C.aiBubbleBorder));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
      ImGui::BeginChild((ImGuiID)(uintptr_t)&e, ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
      // Заголовок
      ImGui::PushStyleColor(ImGuiCol_Text, C.accent);
      ImGui::TextUnformatted("◆ DeepSeek");
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::TextDisabled("· %s", e.title.c_str());
      if (e.streaming) ImGui::SameLine(), ImGui::TextDisabled("· печатает…");
      ImGui::Separator();
      ImGui::PushTextWrapPos(width - 12.0f);
      RenderRichText(e.text);
      if (e.streaming) {
        if (fonts::G().mono) ImGui::PushFont(fonts::G().mono);
        ImGui::PushStyleColor(ImGuiCol_Text, C.accent);
        ImGui::TextUnformatted("▌");
        ImGui::PopStyleColor();
        if (fonts::G().mono) ImGui::PopFont();
      }
      ImGui::PopTextWrapPos();
      ImGui::EndChild();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(2);
      break;
    }
    case ChatEntry::Kind::Tool: {
      // Маленькая схлопнутая записочка про вызов инструмента
      char label[512];
      const char* icon = e.ok ? "⚙" : "⚠";
      std::snprintf(label, sizeof(label), "%s %s", icon, e.title.c_str());
      ImGui::PushStyleColor(ImGuiCol_Text, e.ok ? C.textDim : C.error);
      bool open = ImGui::TreeNodeEx((const void*)&e, ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
                                    label);
      ImGui::PopStyleColor();
      if (open) {
        if (fonts::G().monoSmall) ImGui::PushFont(fonts::G().monoSmall);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(C.toolBubble));
        ImGui::BeginChild((ImGuiID)(uintptr_t)&e + 1, ImVec2(width, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(width - 12.0f);
        ImGui::TextUnformatted(e.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (fonts::G().monoSmall) ImGui::PopFont();
        ImGui::TreePop();
      }
      break;
    }
    case ChatEntry::Kind::Error: {
      ImGui::PushStyleColor(ImGuiCol_Text, C.error);
      ImGui::PushTextWrapPos(width);
      ImGui::TextWrapped("✗ %s", e.text.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
      break;
    }
    case ChatEntry::Kind::Info: {
      ImGui::PushStyleColor(ImGuiCol_Text, C.textDim);
      ImGui::PushTextWrapPos(width);
      ImGui::TextWrapped("%s", e.text.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
      break;
    }
  }
  ImGui::Dummy(ImVec2(0, 2));
}

void ChatPanel::RenderRichText(const std::string& text) {
  // Примитивный markdown-lite: всё между ```...``` — моноширинно на тёмном фоне.
  size_t pos = 0;
  bool inCode = false;
  while (pos < text.size()) {
    size_t fence = text.find("```", pos);
    std::string part = text.substr(pos, fence == std::string::npos ? fence : fence - pos);
    if (!part.empty()) {
      if (inCode) {
        // убрать первую строку с именем языка
        size_t nl = part.find('\n');
        if (nl != std::string::npos && part.substr(0, nl).find_first_not_of(" \t\r") != std::string::npos) {
          // имя языка в первой строке
          std::string lang = part.substr(0, nl);
          if (lang.size() < 24) part = part.substr(nl + 1);
        }
        if (fonts::G().mono) ImGui::PushFont(fonts::G().mono);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(theme::Colors().codeBg));
        ImGui::BeginChild((ImGuiID)(uintptr_t)part.data(), ImVec2(0, 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted(part.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (fonts::G().mono) ImGui::PopFont();
      } else {
        ImGui::TextWrapped("%s", part.c_str());
      }
    }
    if (fence == std::string::npos) break;
    inCode = !inCode;
    pos = fence + 3;
  }
  if (text.empty()) ImGui::TextUnformatted("");
}
