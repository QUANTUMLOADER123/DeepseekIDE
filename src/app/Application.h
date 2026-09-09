#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "ai/Agent.h"
#include "ai/ToolRegistry.h"
#include "core/EditorManager.h"
#include "core/ProjectManager.h"
#include "core/Settings.h"
#include "core/SnapshotManager.h"
#include "ui/ChatPanel.h"
#include "ui/Dialogs.h"
#include "ui/EditorPanel.h"
#include "ui/FileExplorerPanel.h"
#include "ui/LogPanel.h"
#include "ui/SnapshotsPanel.h"
#include "ui/StatusBar.h"

struct GLFWwindow;

// Главный класс DeepSeekIDE: окно, ImGui, панели, события агента.
class Application {
public:
  int Run(int argc, char** argv);

private:
  // --- инициализация/цикл ---
  bool Init(int argc, char** argv);
  void Shutdown();
  void Frame();
  void RenderDockLayout();

  // --- обработчики ---
  void MenuBar();
  void GlobalShortcuts();
  void PollAgentEvents();
  void HandleTabClose();
  void OnExternalFileChanged(const std::string& rel);

  // --- действия ---
  void OpenProject(const std::filesystem::path& path);
  void SendChatMessage(const std::string& text);
  void RollbackLast();
  void DoRollback(std::function<bool(std::string&)> op, const std::string& what);
  void RehydrateChatFromHistory();
  void SaveChatHistory();
  void OpenSettingsDialog();
  std::filesystem::path ChatHistoryPath() const;

  // --- служебное ---
  void Log(const std::string& level, const std::string& msg);
  static void GlfwErrorCallback(int error, const char* description);
  static void GlfwDropCallback(GLFWwindow* window, int count, const char** paths);

  GLFWwindow* mWindow = nullptr;
  float mDpiScale = 1.0f;
  bool mLayoutDone = false;
  bool mFontsDirty = false;
  std::string mCliProject;

  // --- данные ---
  Settings mSettings;
  ProjectManager mProject;
  SnapshotManager mSnaps;
  EditorManager mEditor;
  ToolRegistry* mTools = nullptr;  // владеет через unique-подобный объект ниже
  std::unique_ptr<ToolRegistry> mToolsPtr;
  Agent mAgent;

  // --- UI ---
  FileExplorerPanel mExplorer;
  EditorPanel mEditorPanel;
  ChatPanel mChat;
  SnapshotsPanel mSnapPanel;
  LogPanel mLog;
  StatusBar mStatusBar;
  FolderPickerDialog mFolderDlg;
  SettingsDialog mSettingsDlg;
  AboutDialog mAboutDlg;

  bool mStreaming = false;
  std::string mLastAgentStatus;
  float mStatusBarHeight = 30.0f;
  int mCloseConfirmTab = -1;  // индекс вкладки, для которой показан диалог «Сохранить?»
};
