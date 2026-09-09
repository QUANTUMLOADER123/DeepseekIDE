#pragma once

#include <deque>
#include <mutex>
#include <string>

// Нижняя панель — журнал действий агента, команд и ошибок. Потокобезопасный.
class LogPanel {
public:
  void Add(const std::string& level, const std::string& msg); // из любых потоков
  void Render();
  void Clear();

private:
  struct Line {
    std::string level;
    std::string time;
    std::string text;
  };
  std::mutex mMtx;
  std::deque<Line> mLines;
  size_t mRendered = 0;  // сколько уже забрано в UI
  std::deque<Line> mUi;
  bool mAutoScroll = true;
  bool mShowInfo = true, mShowTool = true, mShowCmd = true, mShowError = true;
};
