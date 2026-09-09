#include "app/Application.h"

#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"  // ImGui::DockBuilder*
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"  // glViewport/glClearColor/glClear

// GLFW_INCLUDE_NONE задаётся через compile-definition в CMake
#include <GLFW/glfw3.h>

#include "app/Platform.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/WebChatWindow.h"
#include "ui/Widgets.h"

// ---------------------------------------------------------------------------

int Application::Run(int argc, char** argv) {
  if (!Init(argc, argv)) {
    Shutdown();
    return 1;
  }

  while (!glfwWindowShouldClose(mWindow)) {
    glfwPollEvents();

    // Пересборка шрифтов строго вне кадра
    if (mFontsDirty) {
      ImGui_ImplOpenGL3_DestroyFontsTexture();
      fonts::LoadAll(ImGui::GetIO(), mDpiScale, mSettings.uiFontSize, mSettings.codeFontSize);
      mFontsDirty = false;
    }

    Frame();
  }

  Log("info", "Завершение работы…");
  SaveChatHistory();
  mSettings.Save();
  mAgent.Cancel();
  Shutdown();
  return 0;
}

void Application::GlfwErrorCallback(int error, const char* description) {
  std::fprintf(stderr, "[GLFW %d] %s\n", error, description ? description : "");
}

void Application::GlfwDropCallback(GLFWwindow* window, int count, const char** paths) {
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (!app || count <= 0) return;
  for (int i = 0; i < count; ++i) {
    std::error_code ec;
    std::filesystem::path p = platform::StrToPath(paths[i]);
    if (std::filesystem::is_directory(p, ec)) {
      app->OpenProject(p);
      return;
    }
  }
  // Кинули файл — открываем его папку как проект и сам файл в редакторе
  std::filesystem::path p = platform::StrToPath(paths[0]);
  app->OpenProject(p.parent_path());
  app->mEditor.Open(p, app->mProject.Root());
}

bool Application::Init(int argc, char** argv) {
  // Аргументы
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if ((a == "--project" || a == "-p") && i + 1 < argc) mCliProject = argv[++i];
    else if (a == "--help" || a == "-h") {
      std::printf("DeepSeekIDE — AI IDE на ImGui\n  deepseekide [--project <папка>]\n");
      return false;
    }
    else if (!a.empty() && a[0] != '-' && mCliProject.empty()) mCliProject = a;
  }

  // Настройки + агентная обвязка
  mSettings = Settings::Load();

  ToolContext tctx;
  tctx.project = &mProject;
  tctx.snapshots = &mSnaps;
  tctx.log = [this](const std::string& lvl, const std::string& msg) { Log(lvl, msg); };
  tctx.fileChanged = [this](const std::string& rel) { OnExternalFileChanged(rel); };
  tctx.allowShell = [this] { return mSettings.allowShell; };
  tctx.shellTimeoutSec = [this] { return mSettings.shellTimeout; };
  mToolsPtr = std::make_unique<ToolRegistry>(std::move(tctx));
  mTools = mToolsPtr.get();
  mAgent.Wire(&mSettings, mTools, &mSnaps);

  // --- GLFW ---
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) {
    std::fprintf(stderr, "glfwInit failed\n");
    return false;
  }

  const char* glsl_version = "#version 130";
#if defined(__APPLE__)
  glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

  mWindow = glfwCreateWindow(1520, 940, "DeepSeekIDE", nullptr, nullptr);
  if (!mWindow) {
    std::fprintf(stderr, "glfwCreateWindow failed\n");
    return false;
  }
  glfwSetWindowUserPointer(mWindow, this);
  glfwSetDropCallback(mWindow, GlfwDropCallback);
  glfwMakeContextCurrent(mWindow);
  glfwSwapInterval(1);  // vsync

  // Масштаб под HiDPI
  float xs = 1.0f, ys = 1.0f;
  glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &xs, &ys);
  mDpiScale = xs > 0.01f ? xs : 1.0f;
  if (mDpiScale < 0.75f) mDpiScale = 0.75f;
  if (mDpiScale > 3.0f) mDpiScale = 3.0f;
  glfwShowWindow(mWindow);

  // --- ImGui ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  io.IniFilename = nullptr;  // сохраняем раскладку сами (см. ConfigDir)

  {
    std::string iniPath = platform::PathToStr(platform::ConfigDir() / "imgui.ini");
    static std::string sIniPath;  // должна пережить вызов
    sIniPath = iniPath;
    io.IniFilename = sIniPath.c_str();
  }

  theme::Apply(mSettings.theme);
  ImGuiStyle& style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  ImGui_ImplGlfw_InitForOpenGL(mWindow, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  fonts::LoadAll(io, mDpiScale, mSettings.uiFontSize, mSettings.codeFontSize);

  // --- Панели: контексты и колбэки ---
  FileExplorerPanel::Callbacks ecb;
  ecb.openFile = [this](const std::filesystem::path& abs) { mEditor.Open(abs, mProject.Root()); };
  ecb.requestOpenFolder = [this] { mFolderDlg.Open(mProject.IsOpen() ? mProject.Root() : platform::HomeDir()); };
  ecb.log = [this](const std::string& msg) { Log("info", msg); };
  mExplorer.SetContext(&mProject, std::move(ecb));

  mEditorPanel.SetContext(&mEditor);

  ChatPanel::Callbacks ccb;
  ccb.send = [this](const std::string& text) { SendChatMessage(text); };
  ccb.stop = [this] { mAgent.Cancel(); mStatusBar.SetStatus("Остановка…"); };
  ccb.clear = [this] {
    mChat.Clear();
    mAgent.ClearHistory();
    SaveChatHistory();
  };
  ccb.openWebChat = [this] {
    if (webchat::Available())
      webchat::EnsureOpen();
    else {
      mChat.AddInfo(webchat::UnavailableReason());
      Log("warn", webchat::UnavailableReason());
    }
  };
  ccb.openSettings = [this] { OpenSettingsDialog(); };
  ccb.rollbackLast = [this] { RollbackLast(); };
  ccb.busy = [this] { return mAgent.Busy(); };
  ccb.hasApiKey = [this] { return !mSettings.apiKey.empty(); };
  ccb.hasProject = [this] { return mProject.IsOpen(); };
  ccb.modelName = [this] { return mSettings.model; };
  mChat.SetCallbacks(std::move(ccb));

  SnapshotsPanel::Callbacks scb;
  scb.rollbackThrough = [this](const std::string& id) {
    DoRollback([this, id](std::string& lg) { return mSnaps.RollbackThrough(id, lg); },
               "действие «" + id + "» и новее");
    mSnapPanel.RequestRefresh();
  };
  scb.rollbackLast = [this] { RollbackLast(); };
  scb.forget = [this](const std::string& id) {
    mSnaps.Delete(id);
    mSnapPanel.RequestRefresh();
  };
  scb.hasProject = [this] { return mProject.IsOpen(); };
  mSnapPanel.SetContext(&mSnaps, std::move(scb));

  StatusBar::Callbacks stcb;
  stcb.projectOpen = [this] { return mProject.IsOpen(); };
  stcb.projectPath = [this] { return mProject.RootStr(); };
  stcb.agentBusy = [this] { return mAgent.Busy(); };
  stcb.modelName = [this] { return mSettings.model; };
  mStatusBar.SetCallbacks(std::move(stcb));
  mStatusBar.SetStatus("Готов");

  mEditor.ApplyStyles(mSettings.editorTabSize, mSettings.showWhitespace);

  // Проект: из аргументов / последний открытый
  std::string start = mCliProject.empty() ? mSettings.lastProject : mCliProject;
  if (!start.empty()) {
    std::error_code ec;
    if (std::filesystem::is_directory(platform::StrToPath(start), ec))
      OpenProject(platform::StrToPath(start));
  }

  Log("info", "DeepSeekIDE запущен: " + platform::NowIso());
  if (!webchat::Available()) Log("warn", std::string("Веб-чат: ") + webchat::UnavailableReason());
  return true;
}

void Application::Shutdown() {
  if (mWindow) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(mWindow);
    mWindow = nullptr;
  }
  glfwTerminate();
}

// ---------------------------------------------------------------------------

void Application::Frame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  PollAgentEvents();
  GlobalShortcuts();
  if (mProject.ConsumeDirty()) {
    mProject.Refresh();
    mSnapPanel.RequestRefresh();
  }

  MenuBar();
  RenderDockLayout();

  // Панели
  mExplorer.Render();
  mEditorPanel.Render();
  mChat.Render();
  mSnapPanel.Render();
  mLog.Render();

  // Статус-бар (возвращает высоту — резервируем в докспейсе на след. кадре)
  mStatusBarHeight = mStatusBar.Render(ImGui::GetIO().Framerate);

  // Диалоги
  if (mFolderDlg.Render()) OpenProject(mFolderDlg.Result());
  if (mSettingsDlg.Render()) {
    theme::Apply(mSettings.theme);
    mEditor.ApplyStyles(mSettings.editorTabSize, mSettings.showWhitespace);
  }
  mAboutDlg.Render();
  HandleTabClose();

  // Рендер
  ImGui::Render();
  int display_w, display_h;
  glfwGetFramebufferSize(mWindow, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  ImVec4 clear = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  glClearColor(clear.x * clear.w, clear.y * clear.w, clear.z * clear.w, clear.w);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    GLFWwindow* backup = glfwGetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    glfwMakeContextCurrent(backup);
  }

  glfwSwapBuffers(mWindow);
}

void Application::RenderDockLayout() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImVec2 workPos = vp->WorkPos;
  ImVec2 workSize = vp->WorkSize;
  workSize.y -= mStatusBarHeight > 1 ? mStatusBarHeight : 30.0f;

  ImGui::SetNextWindowPos(workPos);
  ImGui::SetNextWindowSize(workSize);
  ImGui::SetNextWindowViewport(vp->ID);
  ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##dock_host", nullptr, hostFlags);
  ImGui::PopStyleVar(3);

  ImGuiID dockspace = ImGui::GetID("MainDockspace");
  ImGui::DockSpace(dockspace, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

  if (!mLayoutDone) {
    mLayoutDone = true;
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, workSize);

    ImGuiID dockMain = dockspace;
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.19f, nullptr, &dockMain);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.28f, nullptr, &dockMain);
    ImGuiID dockRightBottom =
        ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.4f, nullptr, &dockRight);
    ImGuiID dockBottom =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.26f, nullptr, &dockMain);

    ImGui::DockBuilderDockWindow("Проект", dockLeft);
    ImGui::DockBuilderDockWindow("DeepSeek Ассистент", dockRight);
    ImGui::DockBuilderDockWindow("Снимки и откат", dockRightBottom);
    ImGui::DockBuilderDockWindow("Журнал", dockBottom);
    ImGui::DockBuilderDockWindow("Редактор", dockMain);
    ImGui::DockBuilderFinish(dockspace);
  }

  ImGui::End();
}

void Application::MenuBar() {
  if (!ImGui::BeginMainMenuBar()) return;

  if (ImGui::BeginMenu("Файл")) {
    if (ImGui::MenuItem("Открыть папку проекта…", "Ctrl+O"))
      mFolderDlg.Open(mProject.IsOpen() ? mProject.Root() : platform::HomeDir());
    ImGui::Separator();
    bool canSave = mEditor.active >= 0 && mEditor.active < (int)mEditor.tabs.size();
    if (ImGui::MenuItem("Сохранить", "Ctrl+S", false, canSave)) {
      std::string err;
      mEditor.Save(mEditor.active, &err);
      if (!err.empty()) Log("error", err);
    }
    if (ImGui::MenuItem("Сохранить всё", "Ctrl+Shift+S", false, mEditor.AnyDirty()))
      mEditor.SaveAll();
    ImGui::Separator();
    if (ImGui::MenuItem("Закрыть вкладку", "Ctrl+W", false, canSave))
      mEditor.RequestClose(mEditor.active);
    ImGui::Separator();
    if (ImGui::MenuItem("Выход")) glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Вид")) {
    for (int i = 0; i < (int)theme::Id::Count; ++i) {
      if (ImGui::MenuItem(theme::Name(i), nullptr, mSettings.theme == i)) {
        mSettings.theme = i;
        theme::Apply(i);
        mSettings.Save();
      }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Шрифт +", "Ctrl+=")) {
      mSettings.uiFontSize = std::min(26, mSettings.uiFontSize + 1);
      mSettings.codeFontSize = std::min(28, mSettings.codeFontSize + 1);
      mFontsDirty = true;
      mSettings.Save();
    }
    if (ImGui::MenuItem("Шрифт −", "Ctrl+-")) {
      mSettings.uiFontSize = std::max(12, mSettings.uiFontSize - 1);
      mSettings.codeFontSize = std::max(10, mSettings.codeFontSize - 1);
      mFontsDirty = true;
      mSettings.Save();
    }
    if (ImGui::MenuItem("Размер шрифта по умолчанию")) {
      mSettings.uiFontSize = 17;
      mSettings.codeFontSize = 16;
      mFontsDirty = true;
      mSettings.Save();
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Проект")) {
    if (ImGui::MenuItem("Обновить файлы", "F5", false, mProject.IsOpen())) mProject.Refresh();
    if (ImGui::MenuItem("Откатить последнее действие агента", nullptr, false, mProject.IsOpen()))
      RollbackLast();
    ImGui::Separator();
    if (ImGui::MenuItem("Забыть текущую папку", nullptr, false, mProject.IsOpen())) {
      SaveChatHistory();
      mProject.Clear();
      mSnaps.SetRoot({});
      mEditor.tabs.clear();
      mEditor.active = -1;
      mSettings.lastProject.clear();
      mSettings.Save();
      mStatusBar.SetStatus("Проект закрыт");
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Чат")) {
    if (ImGui::MenuItem("Открыть chat.deepseek.com в отдельном окне", nullptr, false,
                        webchat::Available()))
      webchat::EnsureOpen();
    ImGui::Separator();
    if (ImGui::MenuItem("Очистить историю чата")) {
      mChat.Clear();
      mAgent.ClearHistory();
      SaveChatHistory();
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Справка")) {
    if (ImGui::MenuItem("О DeepSeekIDE…")) mAboutDlg.Open();
    ImGui::EndMenu();
  }

  // В правой части меню: индикатор агента
  if (mAgent.Busy()) {
    float w = 150.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - w);
    widgets::Spinner("##mb_spin", 6.0f, 2.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("DeepSeek работает…");
  }

  ImGui::EndMainMenuBar();
}

void Application::GlobalShortcuts() {
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
    mFolderDlg.Open(mProject.IsOpen() ? mProject.Root() : platform::HomeDir());
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) OpenSettingsDialog();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
    if (io.KeyShift)
      mEditor.SaveAll();
    else if (mEditor.active >= 0) {
      std::string err;
      int idx = mEditor.active;
      mEditor.Save(idx, &err);
      if (!err.empty()) Log("error", err);
      else Log("info", "Сохранено: " + mEditor.tabs[idx].title);
    }
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W) && mEditor.active >= 0)
    mEditor.RequestClose(mEditor.active);
}

// ---------------------------------------------------------------------------

void Application::PollAgentEvents() {
  auto events = mAgent.DrainEvents();
  for (auto& ev : events) {
    switch (ev.type) {
      case AgentEvent::Type::Status:
        mStatusBar.SetStatus(ev.text);
        mLastAgentStatus = ev.text;
        break;
      case AgentEvent::Type::Token:
        if (!mStreaming) {
          mChat.BeginStream();
          mStreaming = true;
        }
        mChat.AppendStreamDelta(ev.text);
        break;
      case AgentEvent::Type::ToolCall:
        mChat.AddToolCall(ev.text);
        Log("tool", "вызов: " + ev.text);
        break;
      case AgentEvent::Type::ToolResult:
        mChat.AppendToolResult(ev.text, ev.ok);
        break;
      case AgentEvent::Type::Done:
        if (mStreaming) {
          mChat.EndStream();
          mStreaming = false;
        }
        mStatusBar.SetStatus("Готов");
        mProject.Refresh();
        mSnapPanel.RequestRefresh();
        SaveChatHistory();
        break;
      case AgentEvent::Type::Error:
        if (mStreaming) {
          mChat.EndStream();
          mStreaming = false;
        }
        mChat.AddError(ev.text);
        mStatusBar.SetStatus("Ошибка");
        Log("error", ev.text);
        SaveChatHistory();
        break;
      case AgentEvent::Type::Cancelled:
        if (mStreaming) {
          mChat.EndStream();
          mStreaming = false;
        }
        mChat.AddInfo(ev.text);
        mStatusBar.SetStatus("Остановлено");
        mSnapPanel.RequestRefresh();
        SaveChatHistory();
        break;
    }
  }
}

void Application::HandleTabClose() {
  // Новый запрос на закрытие
  if (mEditor.closeRequest >= 0) {
    int idx = mEditor.closeRequest;
    mEditor.closeRequest = -1;
    if (idx >= 0 && idx < (int)mEditor.tabs.size()) {
      if (!mEditor.tabs[idx].Dirty()) {
        mEditor.tabs.erase(mEditor.tabs.begin() + idx);
        if (mEditor.active >= (int)mEditor.tabs.size())
          mEditor.active = (int)mEditor.tabs.size() - 1;
      } else {
        mCloseConfirmTab = idx;  // покажем диалог
        ImGui::OpenPopup("Несохранённые изменения");
      }
    }
  }

  // Диалог подтверждения (рисуется каждый кадр, пока активен)
  if (mCloseConfirmTab < 0 || mCloseConfirmTab >= (int)mEditor.tabs.size()) return;
  auto& tab = mEditor.tabs[mCloseConfirmTab];

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Несохранённые изменения", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Файл «%s» изменён.\nСохранить перед закрытием?", tab.title.c_str());
    ImGui::Dummy(ImVec2(0, 8));
    auto closeTab = [this] {
      mEditor.tabs.erase(mEditor.tabs.begin() + mCloseConfirmTab);
      if (mEditor.active >= (int)mEditor.tabs.size())
        mEditor.active = (int)mEditor.tabs.size() - 1;
      mCloseConfirmTab = -1;
      ImGui::CloseCurrentPopup();
    };
    if (widgets::AccentButton("Сохранить")) {
      std::string err;
      mEditor.Save(mCloseConfirmTab, &err);
      if (!err.empty()) Log("error", err);
      closeTab();
    }
    ImGui::SameLine();
    if (ImGui::Button("Не сохранять")) closeTab();
    ImGui::SameLine();
    if (ImGui::Button("Отмена")) {
      mCloseConfirmTab = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  } else {
    mCloseConfirmTab = -1;  // попап закрыли крестиком/ESC
  }
}

void Application::OnExternalFileChanged(const std::string& rel) {
  std::filesystem::path abs;
  std::string err;
  if (!ProjectManager::SafeJoin(mProject.Root(), rel, abs, err)) return;
  mEditor.OnExternalChange(abs);
  mProject.MarkDirty();  // перестроим дерево на следующем кадре
}

// ---------------------------------------------------------------------------

void Application::OpenProject(const std::filesystem::path& path) {
  SaveChatHistory();  // сохранить историю старого проекта
  mEditor.tabs.clear();
  mEditor.active = -1;

  mProject.SetRoot(path);
  if (!mProject.IsOpen()) {
    mChat.AddError("Не удалось открыть папку: " + platform::PathToStr(path));
    return;
  }
  mSnaps.SetRoot(mProject.Root());

  mSettings.lastProject = platform::PathToStr(mProject.Root());
  mSettings.Save();

  mChat.Clear();
  mAgent.ClearHistory();
  mAgent.LoadHistory(ChatHistoryPath());
  RehydrateChatFromHistory();

  mChat.AddInfo("Проект открыт: " + mProject.RootStr());
  mStatusBar.SetStatus("Проект: " + mProject.Tree().name);
  mSnapPanel.RequestRefresh();
  Log("info", "Открыт проект: " + mProject.RootStr());
}

void Application::SendChatMessage(const std::string& text) {
  mChat.AddUserMessage(text);
  mStatusBar.SetStatus("Отправка…");
  mStreaming = true;
  mChat.BeginStream();
  if (!mAgent.Ask(text)) {
    // Поток занят — крайне маловероятно (кнопка заблокирована)
    mChat.EndStream();
    mStreaming = false;
    mChat.AddError("Агент занят обработкой предыдущего запроса.");
  }
}

void Application::DoRollback(std::function<bool(std::string&)> op, const std::string& what) {
  if (!mProject.IsOpen()) {
    mChat.AddInfo("Сначала откройте папку проекта.");
    return;
  }
  std::string lg;
  op(lg);
  Log("info", "Откат: " + what + "\n" + lg);
  mChat.AddInfo("Откат выполнен (" + what + "):\n" + lg);
  mProject.Refresh();
  for (auto& tab : mEditor.tabs) mEditor.OnExternalChange(tab.absPath);
  mSnapPanel.RequestRefresh();
}

void Application::RollbackLast() {
  DoRollback([this](std::string& lg) { return mSnaps.RollbackLast(lg); }, "последнее действие");
}

void Application::RehydrateChatFromHistory() {
  for (const auto& [role, text] : mAgent.HistoryPairs()) {
    if (text.empty()) continue;
    if (role == "user") mChat.AddUserMessage(text);
    else {
      mChat.BeginStream();
      mChat.AppendStreamDelta(text);
      mChat.EndStream();
    }
  }
}

std::filesystem::path Application::ChatHistoryPath() const {
  return mProject.IsOpen() ? mProject.Root() / ".deepseekide" / "chat_history.json"
                           : platform::ConfigDir() / "chat_history.json";
}

void Application::SaveChatHistory() {
  if (mProject.IsOpen()) mAgent.SaveHistory(ChatHistoryPath());
}

void Application::Log(const std::string& level, const std::string& msg) { mLog.Add(level, msg); }

void Application::OpenSettingsDialog() {
  SettingsDialog::Callbacks cb;
  cb.applyTheme = [this] { theme::Apply(mSettings.theme); };
  cb.rebuildFonts = [this] { mFontsDirty = true; };
  cb.applyEditorStyle = [this] {
    mEditor.ApplyStyles(mSettings.editorTabSize, mSettings.showWhitespace);
  };
  mSettingsDlg.Open(&mSettings, std::move(cb));
}
