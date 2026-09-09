#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "ai/AgentBridge.h"
#include "ai/ToolRegistry.h"
#include "core/EditorManager.h"
#include "core/ProjectManager.h"
#include "core/Settings.h"
#include "core/SnapshotManager.h"
#include "ui/Dialogs.h"
#include "ui/EditorPanel.h"
#include "ui/FileExplorerPanel.h"
#include "ui/LogPanel.h"
#include "ui/SnapshotsPanel.h"
#include "ui/StatusBar.h"
#include "ui/WebChatPanel.h"

struct GLFWwindow;

// Путь к boot.log — используется main.cpp при фатальном исключении до Init.
std::filesystem::path BootLogPathForMain();

// Главный класс DeepSeekIDE: окно, ImGui, встроенный веб-чат chat.deepseek.com
// (левая половина) и редактор с панелями (правая половина).
class Application {
public:
  int Run(int argc, char** argv);

private:
  // --- инициализация/цикл ---
  bool Init(int argc, char** argv);
  void Shutdown();
  void Frame();
  void AttachWebChat();

  // --- отрисовка ---
  void DrawHeader();      // верхняя полоса: логотип, кнопки, статус агента
  void RenderLayout();    // сплиттер 50/50: чат | рабочее пространство
  void DrawAgentBar();    // строка задачи для веб-агента над редактором
  void DrawReplyModal();  // превью ответа/операций + «Применить»
  void DrawChatFallback(float x, float y, float w, float h);  // когда нет webview

  // --- обработчики ---
  void GlobalShortcuts();
  void PumpBridge();
  void HandleTabClose();
  void OnExternalFileChanged(const std::string& rel);

  // --- действия ---
  void OpenProject(const std::filesystem::path& path);
  void RollbackLast();
  void DoRollback(std::function<bool(std::string&)> op, const std::string& what);
  void OpenSettingsDialogInternal();
  void SendTaskToChat();
  void SendActiveFileToChat();
  void ApplyPendingReply();

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
  ToolRegistry* mTools = nullptr;
  std::unique_ptr<ToolRegistry> mToolsPtr;
  WebChatPanel mWebChat;
  AgentBridge mBridge;

  // --- UI ---
  FileExplorerPanel mExplorer;
  EditorPanel mEditorPanel;
  SnapshotsPanel mSnapPanel;
  LogPanel mLog;
  StatusBar mStatusBar;
  FolderPickerDialog mFolderDlg;
  SettingsDialog mSettingsDlg;
  AboutDialog mAboutDlg;

  // --- раскладка ---
  float mHeaderH = 46.0f;
  float mStatusBarHeight = 30.0f;
  float mSplitRatio = 0.5f;

  // --- веб-агент ---
  std::string mTaskInput;                 // текст задачи в строке агента
  bool mReplyModalPending = false;        // пора открыть модал ответа
  AgentBridge::ReplyInfo mPendingReply;   // ответ, который ждёт решения

  int mCloseConfirmTab = -1;  // индекс вкладки, для которой показан диалог «Сохранить?»
};
