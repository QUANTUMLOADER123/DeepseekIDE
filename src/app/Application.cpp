#include "app/Application.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"  // ImGui::DockBuilder*
#include "imgui_stdlib.h"    // InputText с std::string
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"  // glViewport/glClearColor/glClear

// GLFW_INCLUDE_NONE задаётся через compile-definition в CMake
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#  define GLFW_EXPOSE_NATIVE_WIN32
#  include <GLFW/glfw3native.h>
#endif

#include "app/Platform.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace {

ImVec4 Col(const char* hex) {
  // "#RRGGBB"
  unsigned r = 0, g = 0, b = 0;
  std::sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b);
  return ImVec4(r / 255.f, g / 255.f, b / 255.f, 1.f);
}

// Текст по центру текущей строки.
void CenteredText(const char* text) {
  float tw = ImGui::CalcTextSize(text).x;
  float cw = ImGui::GetContentRegionAvail().x;
  if (cw > tw) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cw - tw) * 0.5f);
  ImGui::TextUnformatted(text);
}

}  // namespace

// Маркеры загрузки в %APPDATA%/DeepSeekIDE/boot.log — чтобы понять,
// на каком шаге падает старт: каждый шаг перезаписывает файл своей меткой.
static void BootStep(const char* step) {
  std::string content = std::string("DeepSeekIDE boot log\nstep: ") + step + "\n";
  std::error_code ec;
  std::filesystem::create_directories(platform::ConfigDir(), ec);
  platform::WriteTextFile(platform::ConfigDir() / "boot.log", content);
}

std::filesystem::path BootLogPathForMain() {
  std::error_code ec;
  std::filesystem::create_directories(platform::ConfigDir(), ec);
  return platform::ConfigDir() / "boot.log";
}

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
  mSettings.chatSplit = mSplitRatio;
  mSettings.Save();
  mBridge.Cancel();
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
      std::printf("DeepSeekIDE — AI IDE поверх chat.deepseek.com (без API-ключей)\n"
                  "  deepseekide [--project <папка>]\n");
      return false;
    }
    else if (!a.empty() && a[0] != '-' && mCliProject.empty()) mCliProject = a;
  }

  BootStep("start");
  // Настройки + обвязка инструментов и моста
  mSettings = Settings::Load();
  BootStep("settings loaded");
  mSplitRatio = std::min(0.72f, std::max(0.28f, mSettings.chatSplit));

  ToolContext tctx;
  tctx.project = &mProject;
  tctx.snapshots = &mSnaps;
  tctx.log = [this](const std::string& lvl, const std::string& msg) { Log(lvl, msg); };
  tctx.fileChanged = [this](const std::string& rel) { OnExternalFileChanged(rel); };
  tctx.allowShell = [this] { return mSettings.allowShell; };
  tctx.shellTimeoutSec = [this] { return mSettings.shellTimeout; };
  mToolsPtr = std::make_unique<ToolRegistry>(std::move(tctx));
  mTools = mToolsPtr.get();
  mBridge.Wire(&mWebChat, mTools, &mSnaps, &mProject);
  BootStep("tools wired");

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

  mWindow = glfwCreateWindow(1600, 960, "DeepSeekIDE — агент в чате", nullptr, nullptr);
  if (!mWindow) {
    std::fprintf(stderr, "glfwCreateWindow failed\n");
    return false;
  }
  glfwSetWindowUserPointer(mWindow, this);
  glfwSetDropCallback(mWindow, GlfwDropCallback);
  BootStep("glfw window created");
  glfwMakeContextCurrent(mWindow);
  glfwSwapInterval(1);  // vsync

  // Масштаб под HiDPI
  float xs = 1.0f, ys = 1.0f;
  glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &xs, &ys);
  mDpiScale = xs > 0.01f ? xs : 1.0f;
  if (mDpiScale < 0.75f) mDpiScale = 0.75f;
  if (mDpiScale > 3.0f) mDpiScale = 3.0f;
  glfwShowWindow(mWindow);
  BootStep("window shown");

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
  BootStep("fonts loaded");

  // --- Панели: контексты и колбэки ---
  FileExplorerPanel::Callbacks ecb;
  ecb.openFile = [this](const std::filesystem::path& abs) { mEditor.Open(abs, mProject.Root()); };
  ecb.requestOpenFolder = [this] { mFolderDlg.Open(mProject.IsOpen() ? mProject.Root() : platform::HomeDir()); };
  ecb.log = [this](const std::string& msg) { Log("info", msg); };
  mExplorer.SetContext(&mProject, std::move(ecb));

  mEditorPanel.SetContext(&mEditor);

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
  stcb.agentBusy = [this] { return mBridge.Busy(); };
  stcb.modelName = [] { return std::string("chat.deepseek.com · без API"); };
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

  BootStep("panels wired");

  // Встроенный чат: после того как окно создано.
  AttachWebChat();
  BootStep(mWebChat.Supported() ? "webchat attached" : "webchat: no webview build");

  Log("info", "DeepSeekIDE запущен: " + platform::NowIso());
  if (!mWebChat.Supported())
    Log("warn", "Встроенный браузер не доступен: " + mWebChat.LastError());
  BootStep("init done");
  return true;
}

void Application::AttachWebChat() {
  void* parent = nullptr;
#if defined(_WIN32)
  parent = reinterpret_cast<void*>(glfwGetWin32Window(mWindow));
#endif
  mWebChat.Attach(parent, "https://chat.deepseek.com/");
}

void Application::Shutdown() {
  BootStep("shutdown");
  mWebChat.Detach();
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
  static bool sFirstFrame = true;
  if (sFirstFrame) {
    sFirstFrame = false;
    BootStep("first frame");
  }

  // Разово: если чат не поднялся — заносим в boot.log и в Журнал.
  static bool sWebChatErrLogged = false;
  if (!sWebChatErrLogged && !mWebChat.LastError().empty()) {
    sWebChatErrLogged = true;
    Log("warn", "Веб-чат недоступен: " + mWebChat.LastError());
    std::string s = "webchat error: " + mWebChat.LastError();
    BootStep(s.c_str());
  }
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  PumpBridge();
  GlobalShortcuts();
  if (mProject.ConsumeDirty()) {
    mProject.Refresh();
    mSnapPanel.RequestRefresh();
  }

  DrawHeader();
  RenderLayout();

  // Док-панели правой половины
  mExplorer.Render();
  mEditorPanel.Render();
  mSnapPanel.Render();
  mLog.Render();

  // Статус-бар (возвращает высоту — учитываем в раскладке на след. кадре)
  mStatusBarHeight = mStatusBar.Render(ImGui::GetIO().Framerate);

  // Диалоги
  if (mFolderDlg.Render()) OpenProject(mFolderDlg.Result());
  if (mSettingsDlg.Render()) {
    theme::Apply(mSettings.theme);
    mEditor.ApplyStyles(mSettings.editorTabSize, mSettings.showWhitespace);
  }
  mAboutDlg.Render();
  HandleTabClose();
  DrawReplyModal();

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

// ---------------------------------------------------------------------------

void Application::DrawHeader() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  mHeaderH = 46.0f * mDpiScale;

  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, mHeaderH));
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 6));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Col("#0A0D16"));
  ImGui::Begin("##topbar", nullptr, flags);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  // Логотип
  ImGui::PushStyleColor(ImGuiCol_Text, Col("#4D6BFE"));
  ImGui::TextUnformatted("◆ DeepSeekIDE");
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::TextDisabled(" | ");
  ImGui::SameLine();

  auto headerButton = [&](const char* label, const char* tooltip) -> bool {
    bool clicked = ImGui::Button(label);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    ImGui::SameLine();
    return clicked;
  };

  if (headerButton("Проект", "Открыть папку проекта (Ctrl+O)"))
    mFolderDlg.Open(mProject.IsOpen() ? mProject.Root() : platform::HomeDir());
  if (headerButton("Сохранить", "Сохранить файл (Ctrl+S)")) {
    if (mEditor.active >= 0 && mEditor.active < (int)mEditor.tabs.size()) {
      std::string err;
      mEditor.Save(mEditor.active, &err);
      if (!err.empty()) Log("error", err);
    }
  }
  if (headerButton("Откат", "Откатить последний пакет правок агента")) RollbackLast();
  if (headerButton("Новый чат", "Начать новый чат на chat.deepseek.com")) mWebChat.NewChat();
  if (headerButton("Настройки", "Ctrl+,")) OpenSettingsDialogInternal();
  if (headerButton("?", "О DeepSeekIDE")) mAboutDlg.Open();

  // Правая часть: статус
  const auto st = mWebChat.GetState();
  std::string right;
  ImVec4 rightCol = Col("#8A93AC");
  if (!mWebChat.Supported()) {
    right = "Встроенный браузер не собран";
  } else if (!st.ok) {
    right = "Подключаюсь к chat.deepseek.com…";
  } else if (!st.chatPresent) {
    right = "Войдите в учётку DeepSeek в окне чата слева";
    rightCol = Col("#E8B93A");
  } else if (mBridge.Busy()) {
    right = "… " + mBridge.StatusText();
    rightCol = Col("#4D6BFE");
  } else {
    right = "✔ чат готов";
    rightCol = Col("#34C77B");
  }
  float rw = ImGui::CalcTextSize(right.c_str()).x + 8;
  ImGui::SameLine(ImGui::GetWindowWidth() - rw - 20 * mDpiScale);
  ImGui::PushStyleColor(ImGuiCol_Text, rightCol);
  ImGui::TextUnformatted(right.c_str());
  ImGui::PopStyleColor();

  ImGui::End();
}

// ---------------------------------------------------------------------------

void Application::RenderLayout() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImVec2 base = vp->WorkPos;
  ImVec2 avail = vp->WorkSize;
  avail.y -= mHeaderH + (mStatusBarHeight > 1 ? mStatusBarHeight : 30.0f);
  if (avail.y < 100) avail.y = 100;
  ImVec2 contentPos(base.x, base.y + mHeaderH);

  const float gutter = 6.0f * mDpiScale;
  float splitW = avail.x * mSplitRatio;
  float minLeft = 320.0f * mDpiScale, minRight = 360.0f * mDpiScale;
  splitW = std::min(avail.x - minRight - gutter, std::max(minLeft, splitW));
  mSplitRatio = splitW / avail.x;

  float chatRight = contentPos.x + splitW;

  // --- Сплиттер ---
  ImGui::SetCursorScreenPos(ImVec2(chatRight, contentPos.y));
  ImGui::InvisibleButton("##splitter", ImVec2(gutter, avail.y));
  bool splitterActive = ImGui::IsItemActive();
  if (ImGui::IsItemHovered() || splitterActive)
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  if (splitterActive) {
    float newW = splitW + ImGui::GetIO().MouseDelta.x;
    mSplitRatio = std::min(0.72f, std::max(0.28f, newW / avail.x));
  }
  // Визуальная линия сплиттера
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec4 lineCol = Col(splitterActive ? "#4D6BFE" : "#2A314A");
  float lineX = chatRight + gutter * 0.5f;
  dl->AddLine(ImVec2(lineX, contentPos.y), ImVec2(lineX, contentPos.y + avail.y),
              ImGui::ColorConvertFloat4ToU32(lineCol), splitterActive ? 2.5f : 1.0f);

  // --- Левая половина: чат ---
  if (mWebChat.Supported() && mWebChat.Running()) {
    // окно браузера — дочернее: отдаём прямоугольник в клиентских координатах
    int wx = 0, wy = 0;
    glfwGetWindowPos(mWindow, &wx, &wy);
    bool iconified = glfwGetWindowAttrib(mWindow, GLFW_ICONIFIED) != 0;
    mWebChat.SetRect(static_cast<int>(contentPos.x - wx), static_cast<int>(contentPos.y - wy),
                     static_cast<int>(splitW), static_cast<int>(avail.y));
    mWebChat.SetVisible(!iconified);
  } else {
    DrawChatFallback(contentPos.x, contentPos.y, splitW, avail.y);
  }

  // --- Правая половина: рабочее пространство (агент-бар + докспейс) ---
  ImGui::SetNextWindowPos(ImVec2(chatRight + gutter, contentPos.y));
  ImGui::SetNextWindowSize(ImVec2(avail.x - splitW - gutter, avail.y));
  ImGuiWindowFlags wsFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("##workspace", nullptr, wsFlags);
  ImGui::PopStyleVar(2);

  DrawAgentBar();

  ImGuiID dockspace = ImGui::GetID("WorkspaceDockspace");
  ImVec2 dockSize(0, 0);
  dockSize.y = ImGui::GetContentRegionAvail().y;
  ImGui::DockSpace(dockspace, dockSize, ImGuiDockNodeFlags_PassthruCentralNode);

  if (!mLayoutDone) {
    mLayoutDone = true;
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, dockSize);

    ImGuiID dockMain = dockspace;
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.24f, nullptr, &dockMain);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.26f, nullptr, &dockMain);
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);

    ImGui::DockBuilderDockWindow("Проект", dockLeft);
    ImGui::DockBuilderDockWindow("Снимки и откат", dockRight);
    ImGui::DockBuilderDockWindow("Журнал", dockBottom);
    ImGui::DockBuilderDockWindow("Редактор", dockMain);
    ImGui::DockBuilderFinish(dockspace);
  }

  ImGui::End();
}

void Application::DrawChatFallback(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoSavedSettings;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Col("#0B0E17"));
  ImGui::Begin("##chat_fallback", nullptr, flags);
  ImGui::PopStyleColor();

  ImGui::Dummy(ImVec2(0, h * 0.22f));
  ImGui::PushStyleColor(ImGuiCol_Text, Col("#4D6BFE"));
  CenteredText("◆ DeepSeekIDE");
  ImGui::PopStyleColor();
  ImGui::Spacing();
  ImGui::TextWrapped("Встроенный браузер не собран в этом билде:\n%s",
                     mWebChat.LastError().c_str());
  ImGui::Dummy(ImVec2(0, 12));
  ImGui::TextWrapped(
      "Чтобы заработал встроенный чат chat.deepseek.com, пересоберите IDE с включённым "
      "DEEPSEEKIDE_ENABLE_WEBVIEW (по умолчанию ON). На Windows нужен лишь предустановленный "
      "WebView2 Runtime.\n\n"
      "А пока можно открыть chat.deepseek.com в обычном браузере, общаться с DeepSeek вручную "
      "и применять правки из блоков deepseekide-ops через «Откат»… как только появится "
      "встроенный вариант — всё будет автоматически.");
  ImGui::End();
}

// ---------------------------------------------------------------------------

void Application::DrawAgentBar() {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 7));

  const bool canSend =
      !mBridge.Busy() && mProject.IsOpen() && mWebChat.GetState().chatPresent;

  // Многострочное поле задачи
  float btnW = 60.0f * mDpiScale;
  float sendW = 128.0f * mDpiScale;
  float availX = ImGui::GetContentRegionAvail().x;
  float inputW = availX - sendW - btnW - 12 - ImGui::GetStyle().ItemSpacing.x * 2;
  ImGui::SetNextItemWidth(inputW);

  bool submit = ImGui::InputTextWithHint(
      "##task_input", "Что сделать с проектом? Например: «добавь тёмную тему в консольную утилиту»",
      &mTaskInput, ImGuiInputTextFlags_EnterReturnsTrue);
  if (submit && canSend && !mTaskInput.empty()) SendTaskToChat();

  ImGui::SameLine();
  ImGui::BeginDisabled(!canSend || mTaskInput.empty());
  ImVec4 acc = Col("#4D6BFE");
  ImGui::PushStyleColor(ImGuiCol_Button, acc);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col("#627DFF"));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, Col("#3A58E8"));
  if (ImGui::Button("▶ Отправить", ImVec2(sendW, 0))) SendTaskToChat();
  ImGui::PopStyleColor(3);
  ImGui::EndDisabled();

  // 📎 — прислать активный файл
  ImGui::SameLine();
  bool hasFile = mEditor.active >= 0 && mEditor.active < (int)mEditor.tabs.size();
  ImGui::BeginDisabled(!hasFile || mBridge.Busy());
  if (ImGui::Button("Файл", ImVec2(btnW, 0))) SendActiveFileToChat();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Отправить содержимое активного файла в чат%s",
                      hasFile ? "" : " (сначала откройте файл)");

  ImGui::SameLine();
  if (mBridge.Busy()) {
    widgets::Spinner("##bridge_spin", 7.0f * mDpiScale, 2.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", mBridge.StatusText().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(Esc — снять ожидание)");
  } else if (!mTaskInput.empty()) {
    ImGui::TextDisabled("Enter — отправить");
  }
  if (!mBridge.LastError().empty()) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, Col("#E85B5B"));
    ImGui::TextUnformatted("⚠");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", mBridge.LastError().c_str());
  }

  ImGui::PopStyleVar();
}

void Application::SendTaskToChat() {
  std::string err;
  std::string task = mTaskInput;
  if (mBridge.SendTask(task, err)) {
    mStatusBar.SetStatus("Задача отправлена в чат…");
    Log("agent", "Задача: " + task);
    mTaskInput.clear();
  } else {
    mStatusBar.SetStatus("Не удалось отправить");
    Log("error", "Отправка задачи: " + err);
  }
}

void Application::SendActiveFileToChat() {
  if (mEditor.active < 0 || mEditor.active >= (int)mEditor.tabs.size()) return;
  auto& tab = mEditor.tabs[mEditor.active];
  std::string text = tab.editor.GetText();
  if (text.size() > 120000) text = text.substr(0, 120000) + "\n…(обрезано — файл большой)";
  std::string rel = tab.title;
  std::string note = "Содержимое файла " + rel + ":\n```\n" + text + "\n```";
  std::string err;
  if (mBridge.SendNote(note, err)) {
    mStatusBar.SetStatus("Файл отправлен в чат");
    Log("agent", "Отправлен файл: " + rel);
  } else {
    Log("error", "Отправка файла: " + err);
  }
}

// ---------------------------------------------------------------------------

void Application::PumpBridge() {
  mBridge.Pump();
  AgentBridge::ReplyInfo reply;
  if (mBridge.TakeReadyReply(reply)) {
    mPendingReply = std::move(reply);
    mReplyModalPending = true;
    if (mPendingReply.ops.empty()) {
      mStatusBar.SetStatus("Ответ получен (файловых операций нет)");
      Log("agent", "Ответ без операций:\n" + (mPendingReply.text.size() > 600
                                                 ? mPendingReply.text.substr(0, 600) + "…"
                                                 : mPendingReply.text));
    } else {
      mStatusBar.SetStatus("Ответ получен: операций " +
                           std::to_string(mPendingReply.ops.size()));
    }
  }
}

void Application::DrawReplyModal() {
  if (mReplyModalPending) {
    mReplyModalPending = false;
    ImGui::OpenPopup("Ответ DeepSeek — применить?");
  }
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(720 * mDpiScale, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Ответ DeepSeek — применить?", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;

  auto& rp = mPendingReply;

  if (!rp.text.empty()) {
    std::string preview = rp.text;
    if (preview.size() > 700) preview = preview.substr(0, 700) + "…";
    ImGui::TextDisabled("Сообщение ассистента:");
    ImGui::BeginChild("##reply_text", ImVec2(-1, 120 * mDpiScale), true);
    ImGui::TextWrapped("%s", preview.c_str());
    ImGui::EndChild();
  }

  if (!rp.errs.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, Col("#E8B93A"));
    for (const auto& e : rp.errs) {
      ImGui::Bullet();
      ImGui::TextWrapped("%s", e.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::Separator();
  }

  if (rp.ops.empty()) {
    ImGui::TextUnformatted("Операций с файлами в ответе нет.");
  } else {
    ImGui::Text("Ассистент предлагает %d операций:", (int)rp.ops.size());
    ImGui::BeginChild("##ops_list", ImVec2(-1, std::min(280.0f * mDpiScale, 26.0f * mDpiScale *
                                                                    (float)rp.ops.size() + 24)),
                      true);
    for (size_t i = 0; i < rp.ops.size(); ++i) {
      ImGui::PushID((int)i);
      ImGui::BulletText("%s", ToolRegistry::Describe(rp.ops[i].name, rp.ops[i].args).c_str());
      ImGui::PopID();
    }
    ImGui::EndChild();
  }

  ImGui::Dummy(ImVec2(0, 6));
  bool canApply = !rp.ops.empty() && mProject.IsOpen();
  ImGui::BeginDisabled(!canApply);
  if (widgets::AccentButton("Применить все операции")) ApplyPendingReply();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Пропустить")) {
    Log("info", "Операции из ответа пропущены пользователем.");
    ImGui::CloseCurrentPopup();
  }
  if (!mProject.IsOpen() && !rp.ops.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("(сначала откройте папку проекта)");
  }
  ImGui::EndPopup();
}

void Application::ApplyPendingReply() {
  std::string report = mBridge.ApplyOps(mPendingReply.ops);
  Log("agent", "Применение операций:\n" + report);
  mStatusBar.SetStatus("Операции применены");
  mProject.Refresh();
  mSnapPanel.RequestRefresh();
  for (auto& tab : mEditor.tabs) mEditor.OnExternalChange(tab.absPath);
  ImGui::CloseCurrentPopup();
}

// ---------------------------------------------------------------------------

void Application::GlobalShortcuts() {
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
    mFolderDlg.Open(mProject.IsOpen() ? mProject.Root() : platform::HomeDir());
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) OpenSettingsDialogInternal();
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
  if (ImGui::IsKeyPressed(ImGuiKey_Escape) && mBridge.Busy() && !ImGui::IsAnyItemActive()) {
    mBridge.Cancel();
    mStatusBar.SetStatus("Ожидание ответа снято");
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
  mEditor.tabs.clear();
  mEditor.active = -1;

  mProject.SetRoot(path);
  if (!mProject.IsOpen()) {
    Log("error", "Не удалось открыть папку: " + platform::PathToStr(path));
    mStatusBar.SetStatus("Не удалось открыть проект");
    return;
  }
  mSnaps.SetRoot(mProject.Root());

  mSettings.lastProject = platform::PathToStr(mProject.Root());
  mSettings.Save();

  mStatusBar.SetStatus("Проект: " + mProject.Tree().name);
  mSnapPanel.RequestRefresh();
  Log("info", "Открыт проект: " + mProject.RootStr());
}

void Application::DoRollback(std::function<bool(std::string&)> op, const std::string& what) {
  if (!mProject.IsOpen()) {
    Log("warn", "Откат: сначала откройте папку проекта.");
    return;
  }
  std::string lg;
  op(lg);
  Log("info", "Откат: " + what + "\n" + lg);
  mProject.Refresh();
  for (auto& tab : mEditor.tabs) mEditor.OnExternalChange(tab.absPath);
  mSnapPanel.RequestRefresh();
}

void Application::RollbackLast() {
  DoRollback([this](std::string& lg) { return mSnaps.RollbackLast(lg); }, "последнее действие");
}

void Application::Log(const std::string& level, const std::string& msg) { mLog.Add(level, msg); }

void Application::OpenSettingsDialogInternal() {
  SettingsDialog::Callbacks cb;
  cb.applyTheme = [this] { theme::Apply(mSettings.theme); };
  cb.rebuildFonts = [this] { mFontsDirty = true; };
  cb.applyEditorStyle = [this] {
    mEditor.ApplyStyles(mSettings.editorTabSize, mSettings.showWhitespace);
  };
  mSettingsDlg.Open(&mSettings, std::move(cb));
}
