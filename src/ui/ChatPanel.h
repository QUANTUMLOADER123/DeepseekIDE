#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

// Одна запись в ленте чата.
struct ChatEntry {
  enum class Kind { User, Assistant, Tool, Error, Info } kind = Kind::Info;
  std::string title;     // для Tool: краткое описание; для остальных — время
  std::string text;      // тело
  bool ok = true;        // для Tool/Error
  bool streaming = false;
};

// Правая панель — чат с DeepSeek + агент + быстрые действия.
class ChatPanel {
public:
  struct Callbacks {
    std::function<void(const std::string&)> send;
    std::function<void()> stop;
    std::function<void()> clear;
    std::function<void()> openWebChat;      // chat.deepseek.com в отдельном окне
    std::function<void()> openSettings;
    std::function<void()> rollbackLast;
    std::function<bool()> busy;
    std::function<bool()> hasApiKey;
    std::function<bool()> hasProject;
    std::function<std::string()> modelName;
  };

  void SetCallbacks(Callbacks cb) { mCb = std::move(cb); }

  void Render();

  // --- события от Application ---
  void AddUserMessage(const std::string& text);
  void AddInfo(const std::string& text);
  void AddError(const std::string& text);
  void AddToolCall(const std::string& describe);
  void AppendToolResult(const std::string& text, bool ok);
  void BeginStream();                          // старт стримящегося ответа
  void AppendStreamDelta(const std::string&);  // токены
  void EndStream();
  void Clear();
  bool Empty() const { return mEntries.empty(); }

private:
  void RenderEntry(const ChatEntry& e);
  void RenderRichText(const std::string& text);  // markdown-lite: блоки ``` в моно-шрифте

  Callbacks mCb;
  std::vector<ChatEntry> mEntries;
  std::string mInput;
  bool mScrollToBottom = false;
  int mStreamIndex = -1;
};
