#pragma once

#include <functional>
#include <mutex>
#include <string>

// Нижняя строка состояния через всё окно: проект · статус агента · модель · FPS.
class StatusBar {
public:
  struct Callbacks {
    std::function<bool()> projectOpen;
    std::function<std::string()> projectPath;
    std::function<bool()> agentBusy;
    std::function<std::string()> modelName;
  };
  void SetCallbacks(Callbacks cb) { mCb = std::move(cb); }

  void SetStatus(const std::string& s);
  std::string Status() const;
  float Render(float fps);  // возвращает высоту бара

private:
  Callbacks mCb;
  mutable std::mutex mMtx;
  std::string mStatus;
};
