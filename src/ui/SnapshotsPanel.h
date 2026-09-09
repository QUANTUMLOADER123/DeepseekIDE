#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/SnapshotManager.h"

// Панель истории снимков — откат действий агента.
class SnapshotsPanel {
public:
  struct Callbacks {
    std::function<void(const std::string& id)> rollbackThrough; // откат: это действие + все новее
    std::function<void()> rollbackLast;
    std::function<void(const std::string& id)> forget;          // удалить снимок
    std::function<bool()> hasProject;
  };

  void SetContext(const SnapshotManager* snaps, Callbacks cb) {
    mSnaps = snaps;
    mCb = std::move(cb);
  }
  void Render();
  void RequestRefresh() { mDirty = true; }

private:
  const SnapshotManager* mSnaps = nullptr;
  Callbacks mCb;
  std::vector<SnapshotInfo> mCache;
  bool mDirty = true;
  bool mOpenFiles = true;
};
