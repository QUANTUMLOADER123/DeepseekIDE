#include "mainwindow.h"
#include "codeeditor.h"
#include "deepseekclient.h"
#include "agent.h"

#include <QTreeView>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QDockWidget>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QShortcut>
#include <QApplication>
#include <QFileInfo>
#include <QFile>
#include <QTextOption>
#include <QScrollBar>
#include <QRegularExpression>
#include <QFrame>
#include <QDir>
#include <QMenuBar>
#include <QPixmap>
#include <QTimer>
#include <QTextDocument>
#include <QGraphicsOpacityEffect>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QStyle>
#include <QEasingCurve>
#include <QAbstractAnimation>

// ============================================================================
//  Тема
// ============================================================================
static const char* kDark = R"(
/* ============ DeepSeekIDE — dark pro ============ */
QMainWindow, QDialog { background:#0d0f15; }
QWidget { color:#e9ecf6; font-size:13px; font-family:'Segoe UI','Inter',sans-serif; background:transparent; }
*[themeRoot="true"] { background:#0d0f15; }

QMenuBar { background:#111420; border-bottom:1px solid #1d2130; padding:2px; }
QMenuBar::item { padding:6px 14px; border-radius:8px; background:transparent; color:#c6cbdf; }
QMenuBar::item:selected { background:#252a41; color:#ffffff; }
QMenu { background:#151826; border:1px solid #262b42; border-radius:12px; padding:6px; }
QMenu::item { padding:7px 26px; border-radius:8px; }
QMenu::item:selected { background:#2d3253; }

QFrame#panel { background:#11141f; border:1px solid #202537; border-radius:14px; }
QLabel#panelTitle { color:#697093; font-size:11px; font-weight:700; padding:2px 4px 0 4px; }
QLabel#panelTitleAccent { color:#8d83ff; font-size:11px; font-weight:700; padding:2px 4px 0 4px; }
QFrame#accentBar { background:#8d83ff; border-radius:1px; max-height:2px; min-height:2px; max-width:44px; min-width:44px; }
QFrame#accentBarGray { background:#333950; border-radius:1px; max-height:2px; min-height:2px; max-width:44px; min-width:44px; }

QTreeView { background:transparent; border:none; outline:none; padding:2px; }
QTreeView::item { padding:6px 8px; border-radius:7px; color:#c4c9e0; }
QTreeView::item:hover { background:#1b1f30; }
QTreeView::item:selected { background:#282d4c; color:#ded6ff; }

QTabWidget::pane { background:#0a0c12; border:1px solid #202537; border-radius:12px; top:30px; }
QTabBar { background:transparent; }
QTabBar::tab {
  background:transparent; color:#757c9e;
  padding:10px 20px; margin:6px 2px 0 2px;
  border-top-left-radius:10px; border-top-right-radius:10px;
  border-bottom:2px solid transparent;
}
QTabBar::tab:hover { color:#c6cbe4; background:#181c2b; }
QTabBar::tab:selected { color:#ffffff; background:#131729; border-bottom:2px solid #8d83ff; }
QTabBar::close-button { subcontrol-position:right; margin:4px; border-radius:4px; }
QTabBar::close-button:hover { background:#40202e; }

QPlainTextEdit {
  background:transparent; border:none; padding:6px;
  selection-background-color:#3c4180; selection-color:#ffffff;
}
QPlainTextEdit#composer {
  background:#141827; border:1px solid #262b47; border-radius:14px; padding:12px 16px;
}
QPlainTextEdit#composer:focus { border-color:#8d83ff; }
QPlainTextEdit#composer:disabled { background:#10131e; color:#596080; }
QPlainTextEdit#logView { font-family:'Cascadia Code','Consolas',monospace; font-size:12px; color:#8d93af; }

QFrame[role="user"] {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #333a78, stop:1 #262b58);
  border:1px solid #454da4; border-radius:16px;
}
QFrame[role="user"] QLabel { color:#eef0ff; }
QFrame[role="assistant"] { background:#141826; border:1px solid #232941; border-radius:16px; }
QFrame[role="assistant"][state="live"] { border-color:#8d83ff; }
QFrame[role="tool"] { background:#0f121c; border:1px solid #1e2334; border-left:3px solid #2fbf71; border-radius:10px; }
QFrame[role="tool"]:hover { background:#131726; }
QFrame[role="tool"][state="fail"] { border-left-color:#ff5470; }
QFrame[role="tool"] QLabel { font-family:'Cascadia Code','Consolas',monospace; font-size:12px; }
QFrame[role="error"] { background:#2c141d; border:1px solid #582438; border-radius:12px; }
QFrame[role="hint"] { background:#12151f; border:1px dashed #313752; border-radius:12px; }
QLabel { background:transparent; }

QPushButton {
  background:#212638; border:1px solid #2d3350; border-radius:10px;
  padding:8px 18px; color:#dfe3f5;
}
QPushButton:hover { background:#2a3048; border-color:#3d4470; }
QPushButton:pressed { background:#1d2233; }
QPushButton:disabled { background:#161a28; color:#4d5378; border-color:#202538; }
QPushButton#sendBtn {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #6d5cff, stop:1 #a06bff);
  border:none; border-radius:14px; padding:12px 30px; color:#ffffff; font-weight:700; font-size:13px;
}
QPushButton#sendBtn:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8270ff, stop:1 #b07eff); }
QPushButton#sendBtn:pressed { background:#5848e0; }
QPushButton#sendBtn:disabled { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #d4506a, stop:1 #b03a56); color:#fff; }

QLineEdit, QSpinBox, QComboBox {
  background:#141827; border:1px solid #262b47; border-radius:8px; padding:7px 12px; min-height:20px;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color:#8d83ff; }
QComboBox QAbstractItemView { background:#151826; border:1px solid #262b42; selection-background-color:#2d3253; }
QComboBox::drop-down { border:none; width:24px; }
QCheckBox { spacing:8px; }
QCheckBox::indicator { width:16px; height:16px; border-radius:5px; border:1px solid #3a4166; background:#141827; }
QCheckBox::indicator:checked { background:#6d5cff; border-color:#6d5cff; }

QDockWidget { color:#697093; font-weight:600; }
QDockWidget::title { background:#0f121c; padding:9px 14px; border-top:1px solid #202537; }

QSplitter::handle { background:transparent; }
QSplitter::handle:horizontal { width:3px; }
QSplitter::handle:horizontal:hover { background:#333a63; }

QScrollBar:vertical { background:transparent; width:10px; margin:4px 2px; }
QScrollBar::handle:vertical { background:#262b43; border-radius:5px; min-height:36px; }
QScrollBar::handle:vertical:hover { background:#3b4170; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar:horizontal { background:transparent; height:10px; margin:2px 4px; }
QScrollBar::handle:horizontal { background:#262b43; border-radius:5px; min-width:36px; }
QScrollBar::handle:horizontal:hover { background:#3b4170; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }

QScrollArea { border:none; background:transparent; }
QToolTip { background:#1b1e30; color:#e9ecf6; border:1px solid #2e3354; padding:7px 10px; border-radius:8px; }
)";

// ============================================================================
MainWindow::MainWindow() {
    cfg_ = Cfg::load();
    setWindowTitle(QStringLiteral("DeepSeek C++ IDE"));
    resize(1460, 920);
    qApp->setStyleSheet(QLatin1String(kDark));

    ds_ = new DeepSeekClient(this);
    ds_->setCfg(cfg_);
    agent_ = new Agent(ds_, this);
    agent_->setCfg(cfg_);
    agent_->setProjectRoot(cfg_.projectDir);

    auto* menu = menuBar()->addMenu(QStringLiteral("Файл"));
    QAction* aFolder = menu->addAction(QStringLiteral("Открыть папку…"));
    QAction* aSave = menu->addAction(QStringLiteral("Сохранить (Ctrl+S)"));
    QAction* aNewChat = menu->addAction(QStringLiteral("Новый чат"));
    QAction* aSettings = menu->addAction(QStringLiteral("Настройки…"));
    connect(aFolder, &QAction::triggered, this, &MainWindow::onPickFolder);
    connect(aSave, &QAction::triggered, this, &MainWindow::saveCurrent);
    connect(aNewChat, &QAction::triggered, this, &MainWindow::onNewChat);
    connect(aSettings, &QAction::triggered, this, &MainWindow::onSettings);
    new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_S)), this, SLOT(saveCurrent()));

    // ---- дерево
    fsModel_ = new QFileSystemModel(this);
    fsModel_->setReadOnly(true);
    fsModel_->setNameFilters(QStringList{});
    tree_ = new QTreeView;
    tree_->setModel(fsModel_);
    tree_->setHeaderHidden(true);
    tree_->setAnimated(false);
    tree_->setIndentation(14);
    for (int i = 1; i < 4; ++i) tree_->hideColumn(i);
    connect(tree_, &QTreeView::doubleClicked, this, &MainWindow::onTreeOpen);

    // фабрика панели с шапкой + акцентной полоской
    auto mkPanel = [](const QString& title, bool accent) {
        auto* f = new QFrame; f->setObjectName(QStringLiteral("panel"));
        auto* l = new QVBoxLayout(f);
        l->setContentsMargins(14, 12, 14, 14);
        l->setSpacing(8);
        auto* t = new QLabel(title);
        t->setObjectName(accent ? QStringLiteral("panelTitleAccent") : QStringLiteral("panelTitle"));
        auto* bar = new QFrame;
        bar->setObjectName(accent ? QStringLiteral("accentBar") : QStringLiteral("accentBarGray"));
        l->addWidget(t);
        l->addWidget(bar);
        return f;
    };
    auto* projPanel = mkPanel(QStringLiteral("ФАЙЛЫ ПРОЕКТА"), false);
    projPanel->layout()->addWidget(tree_);

    // ---- центр: табы
    tabs_ = new QTabWidget;
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabClose);
    auto* editorPanel = mkPanel(QStringLiteral("РЕДАКТОР"), false);
    editorPanel->layout()->addWidget(tabs_);

    // ---- чат
    chatScroll_ = new QScrollArea;
    chatScroll_->setWidgetResizable(true);
    auto* chatHost = new QWidget;
    chatLay_ = new QVBoxLayout(chatHost);
    chatLay_->setContentsMargins(10, 10, 10, 10);
    chatLay_->setSpacing(10);
    chatLay_->addStretch(1);
    chatScroll_->setWidget(chatHost);

    input_ = new QPlainTextEdit;
    input_->setObjectName(QStringLiteral("composer"));
    input_->setPlaceholderText(QStringLiteral("Напишите сообщение…  (Ctrl+Enter — отправить)"));
    input_->setMaximumHeight(100);
    input_->setMinimumHeight(66);
    input_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Return)), this, SLOT(onSend()));
    new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Enter)), this, SLOT(onSend()));
    sendBtn_ = new QPushButton(QStringLiteral("Отправить"));
    sendBtn_->setObjectName(QStringLiteral("sendBtn"));
    sendBtn_->setCursor(Qt::PointingHandCursor);
    connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::onSend);

    auto* chatPanel = mkPanel(QStringLiteral("DEEPSEEK АГЕНТ"), true);
    auto* cl2 = qobject_cast<QVBoxLayout*>(chatPanel->layout());
    cl2->addWidget(chatScroll_, 1);
    auto* inputRow = new QWidget;
    auto* irl = new QHBoxLayout(inputRow);
    irl->setContentsMargins(0, 0, 0, 0);
    irl->setSpacing(10);
    irl->addWidget(input_, 1);
    irl->addWidget(sendBtn_, 0, Qt::AlignBottom);
    cl2->addWidget(inputRow);

    auto* mainSplit = new QSplitter;
    mainSplit->addWidget(projPanel);
    mainSplit->addWidget(editorPanel);
    mainSplit->addWidget(chatPanel);
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setStretchFactor(2, 0);
    mainSplit->setSizes({270, 780, 440});

    logView_ = new QPlainTextEdit;
    logView_->setObjectName(QStringLiteral("logView"));
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(2000);
    auto* logDock = new QDockWidget(QStringLiteral("ЛОГ"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    logDock->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->setMinimumHeight(110);

    auto* central = new QWidget;
    central->setProperty("themeRoot", true);
    auto* cl = new QVBoxLayout(central);
    cl->setContentsMargins(10, 10, 10, 10);
    cl->setSpacing(10);
    cl->addWidget(mainSplit);
    setCentralWidget(central);

    rebuildTreeRoot();

    // вайринг
    connect(agent_, &Agent::appendUserBubble, this, &MainWindow::addUser);
    connect(agent_, &Agent::assistantBubbleStart, this, &MainWindow::beginAssistant);
    connect(agent_, &Agent::assistantDelta, this, &MainWindow::appendAssistantDelta);
    connect(agent_, &Agent::assistantBubbleDone, this, &MainWindow::endAssistant);
    connect(agent_, &Agent::toolRow, this, &MainWindow::addToolRow);
    connect(agent_, &Agent::imageShown, this, &MainWindow::addImage);
    connect(agent_, &Agent::errorBanner, this, &MainWindow::addError);
    connect(agent_, &Agent::busyChanged, this, &MainWindow::onBusy);
    connect(agent_, &Agent::log, this, &MainWindow::logLine);
    connect(ds_, &DeepSeekClient::log, this, &MainWindow::logLine);

    connect(&pulseTimer_, &QTimer::timeout, this, &MainWindow::pulseTick);
    pulseTimer_.setInterval(160);

    logLine(cfg_.token.isEmpty()
        ? QStringLiteral("Токен не задан. Откройте Настройки и вставьте Bearer-токен DeepSeek.")
        : QStringLiteral("Готово. Пишите сообщение."));
}

// ============================================================================
//  карточки чата
// ============================================================================
QFrame* MainWindow::chatAddCard(const QString& role, const QString& state) {
    auto* f = new QFrame;
    f->setProperty("role", role);
    if (!state.isEmpty()) f->setProperty("state", state);
    auto* l = new QVBoxLayout(f);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(6);
    // мягкая тень
    auto* sh = new QGraphicsDropShadowEffect(f);
    sh->setBlurRadius(28);
    sh->setOffset(0, 5);
    sh->setColor(QColor(0, 0, 0, 110));
    f->setGraphicsEffect(sh);
    return f;
}

QWidget* MainWindow::chatAppend(const QString& role, const QString& state) {
    QFrame* card = chatAddCard(role, state);
    auto* wrap = new QWidget;
    auto* hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(0, 0, 0, 0);
    if (role == QStringLiteral("user")) {
        hl->addStretch(1);
        card->setMaximumWidth(660);
        hl->addWidget(card, 0);
    } else {
        hl->addWidget(card, 0);
        if (role == QStringLiteral("assistant") || role == QStringLiteral("error"))
            card->setMaximumWidth(760);
        hl->addStretch(1);
    }
    chatLay_->insertWidget(chatLay_->count() - 1, wrap);
    fadeIn(wrap);
    smoothScrollBottom();
    return card;
}

void MainWindow::fadeIn(QWidget* w, int ms) {
    auto* fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    auto* anim = new QPropertyAnimation(fx, "opacity", w);
    anim->setDuration(ms);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QPropertyAnimation::finished, w, [w, fx]() {
        w->setGraphicsEffect(nullptr);
        fx->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::smoothScrollBottom() {
    QScrollBar* sb = chatScroll_->verticalScrollBar();
    auto* anim = new QPropertyAnimation(sb, "value", sb);
    anim->setDuration(180);
    anim->setStartValue(sb->value());
    anim->setEndValue(sb->maximum());
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::pulseTick() {
    if (!liveCard_) return;
    // дышащая рамка у «живой» карточки ассистента
    static const char* cols[] = { "#8d83ff", "#a89fff", "#c3bcff", "#a89fff" };
    pulsePhase_ = (pulsePhase_ + 1) % 4;
    liveCard_->setStyleSheet(QStringLiteral(
        "QFrame[role=\"assistant\"]{background:#141826;border:1px solid %1;border-radius:16px;}")
        .arg(QLatin1String(cols[pulsePhase_])));
}

QString MainWindow::mdToHtml(const QString& md) {
    QString s = md.toHtmlEscaped();
    s.replace(QRegularExpression(QStringLiteral("```(\\w*)\\n([\\s\\S]*?)```")),
              QStringLiteral("<pre style='background:#0a0c12;border:1px solid #262b47;border-radius:8px;padding:10px;font-family:Consolas,monospace;'>\\2</pre>"));
    s.replace(QRegularExpression(QStringLiteral("`([^`\\n]+)`")),
              QStringLiteral("<code style='background:#0a0c12;border:1px solid #262b47;border-radius:4px;padding:1px 6px;font-family:Consolas,monospace;'>\\1</code>"));
    s.replace(QRegularExpression(QStringLiteral("^####?\\s+(.+)$"), QRegularExpression::MultilineOption),
              QStringLiteral("<b style='color:#b3adff;font-size:14px'>\\1</b>"));
    s.replace(QRegularExpression(QStringLiteral("\\*\\*([^*\\n]+)\\*\\*")),
              QStringLiteral("<b>\\1</b>"));
    s.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return s;
}

// ============================================================================
//  файлы
// ============================================================================
void MainWindow::rebuildTreeRoot() {
    const QString dir = cfg_.projectDir.isEmpty() ? QDir::currentPath() : cfg_.projectDir;
    const QModelIndex root = fsModel_->setRootPath(dir);
    tree_->setRootIndex(root);
    setWindowTitle(QStringLiteral("DeepSeek C++ IDE — %1").arg(QDir(dir).dirName()));
}

void MainWindow::onTreeOpen(const QModelIndex& idx) {
    if (fsModel_->isDir(idx)) return;
    openFile(fsModel_->filePath(idx));
}

CodeEditor* MainWindow::openFile(const QString& absPath) {
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* ed = qobject_cast<CodeEditor*>(tabs_->widget(i));
        if (ed && ed->path() == absPath) { tabs_->setCurrentIndex(i); return ed; }
    }
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) { addError(QStringLiteral("Не открывается: ") + absPath); return nullptr; }
    QByteArray raw = f.readAll();
    auto* ed = new CodeEditor;
    ed->setPath(absPath);
    ed->setPlainText(QString::fromUtf8(raw));
    ed->setLanguageByExt(QFileInfo(absPath).suffix());
    ed->markSaved();
    connect(ed->document(), &QTextDocument::modificationChanged, this, &MainWindow::onTabTitleChanged);
    int i = tabs_->addTab(ed, QFileInfo(absPath).fileName());
    tabs_->setTabToolTip(i, absPath);
    tabs_->setCurrentIndex(i);
    QTimer::singleShot(0, ed, [ed]{ ed->markSaved(); });
    return ed;
}

void MainWindow::saveCurrent() {
    auto* ed = qobject_cast<CodeEditor*>(tabs_->currentWidget());
    if (!ed || ed->path().isEmpty()) return;
    QFile f(ed->path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, QStringLiteral("Сохранение"), f.errorString());
        return;
    }
    f.write(ed->toPlainText().toUtf8());
    f.close();
    ed->markSaved();
    logLine(QStringLiteral("Сохранено: %1").arg(ed->path()));
}

void MainWindow::onTabClose(int i) {
    auto* ed = qobject_cast<CodeEditor*>(tabs_->widget(i));
    if (ed && ed->modified()) {
        auto r = QMessageBox::question(this, QStringLiteral("Закрыть"),
            QStringLiteral("%1 изменён, закрыть без сохранения?").arg(QFileInfo(ed->path()).fileName()));
        if (r != QMessageBox::Yes) return;
    }
    QWidget* w = tabs_->widget(i);
    tabs_->removeTab(i);
    if (w) w->deleteLater();
}

void MainWindow::onTabTitleChanged() {
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* ed = qobject_cast<CodeEditor*>(tabs_->widget(i));
        if (!ed) continue;
        QString t = QFileInfo(ed->path()).fileName();
        if (ed->modified()) t.prepend(QStringLiteral("• "));
        tabs_->setTabText(i, t);
    }
}

// ============================================================================
//  чат-поведение
// ============================================================================
void MainWindow::onSend() {
    if (busyOn_) { // кнопка становится "Стоп"
        ds_->abort();
        return;
    }
    QString t = input_->toPlainText().trimmed();
    if (t.isEmpty()) return;
    input_->clear();
    agent_->sendUserMessage(t);
}

void MainWindow::onBusy(bool on) {
    busyOn_ = on;
    sendBtn_->setText(on ? QStringLiteral("Стоп") : QStringLiteral("Отправить"));
    sendBtn_->setEnabled(true); // кнопка всегда живая: Стоп работает при busy
    input_->setDisabled(on);
    input_->setPlaceholderText(on ? QStringLiteral("Агент пишет/работает…")
                                  : QStringLiteral("Напишите сообщение…  (Ctrl+Enter — отправить)"));
}

void MainWindow::addUser(const QString& text) {
    QFrame* card = qobject_cast<QFrame*>(chatAppend(QStringLiteral("user")));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(mdToHtml(text));
    lbl->setWordWrap(true);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(lbl);
}

void MainWindow::beginAssistant() {
    liveCard_ = qobject_cast<QFrame*>(chatAppend(QStringLiteral("assistant"), QStringLiteral("live")));
    auto* l = qobject_cast<QVBoxLayout*>(liveCard_->layout());
    liveBubble_ = new QLabel(QStringLiteral("<span style='color:#5a5f8a;font-style:italic'>печатает…</span>"));
    liveBubble_->setWordWrap(true);
    liveBubble_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(liveBubble_);
    liveRaw_.clear();
    pulsePhase_ = 0;
    pulseTimer_.start();
}

void MainWindow::appendAssistantDelta(const QString& delta) {
    if (!liveBubble_) beginAssistant();
    liveRaw_ += delta;
    liveBubble_->setText(liveRaw_);
    smoothScrollBottom();
}

void MainWindow::endAssistant(const QString& cleanText) {
    pulseTimer_.stop();
    if (liveBubble_) {
        liveBubble_->setText(cleanText.isEmpty()
            ? QStringLiteral("<span style='color:#5a5f8a;font-style:italic'>…агент работает дальше —</span>")
            : mdToHtml(cleanText));
    }
    if (liveCard_) {
        liveCard_->setStyleSheet(QString()); // снять пульсовый inline-стиль
        liveCard_->setProperty("state", QString());
        liveCard_->style()->unpolish(liveCard_);
        liveCard_->style()->polish(liveCard_);
        liveCard_ = nullptr;
        liveBubble_ = nullptr;
        liveRaw_.clear();
    }
    smoothScrollBottom();
}

void MainWindow::addToolRow(const QString& name, const QString& sub, bool ok, const QString& detail) {
    QFrame* card = qobject_cast<QFrame*>(chatAppend(QStringLiteral("tool"), ok ? QString() : QStringLiteral("fail")));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(QStringLiteral("<b style='color:%1'>%2</b> <span style='color:#697093'>%3</span>")
                               .arg(ok ? QStringLiteral("#35cf7f") : QStringLiteral("#ff5470"),
                                    name.toHtmlEscaped(), sub.toHtmlEscaped()));
    lbl->setWordWrap(true);
    l->addWidget(lbl);
    if (!detail.isEmpty()) {
        auto* det = new QLabel(QStringLiteral("<pre style='color:#8b91ad;font-size:11px;'>%1</pre>")
                                   .arg(detail.left(1500).toHtmlEscaped()));
        det->setWordWrap(true);
        l->addWidget(det);
    }
}

void MainWindow::addImage(const QByteArray& bytes, const QString& mime, const QString& alt) {
    Q_UNUSED(mime);
    QPixmap pm;
    if (!pm.loadFromData(bytes)) { addError(QStringLiteral("Картинка не декодирована")); return; }
    QFrame* card = qobject_cast<QFrame*>(chatAppend(QStringLiteral("assistant")));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel;
    lbl->setPixmap(pm.scaledToWidth(240, Qt::SmoothTransformation));
    lbl->setToolTip(alt.isEmpty() ? QStringLiteral("image") : alt);
    l->addWidget(lbl);
}

void MainWindow::addError(const QString& err) {
    pulseTimer_.stop();
    QFrame* card = qobject_cast<QFrame*>(chatAppend(QStringLiteral("error")));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(QStringLiteral("<b style='color:#ff5470'>Ошибка</b><br><span style='color:#ffb7c5'>%1</span>").arg(err.toHtmlEscaped()));
    lbl->setWordWrap(true);
    l->addWidget(lbl);
}

void MainWindow::logLine(const QString& line) {
    logView_->appendPlainText(line);
}

// ============================================================================
//  верхние действия
// ============================================================================
void MainWindow::onNewChat() {
    ds_->newChat();
    agent_->newChat();
    liveCard_ = nullptr; liveBubble_ = nullptr; liveRaw_.clear();
    pulseTimer_.stop();
    for (int i = chatLay_->count() - 2; i >= 0; --i) {
        QLayoutItem* it = chatLay_->itemAt(i);
        if (!it) continue;
        delete it->widget();
        chatLay_->removeItem(it);
    }
    QFrame* card = qobject_cast<QFrame*>(chatAppend(QStringLiteral("hint")));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* h = new QLabel(QStringLiteral("<span style='color:#7d84a6'>Новый чат. Агент умеет читать и править файлы проекта, выполнять команды, ходить в интернет и генерировать картинки — просто опишите задачу.</span>"));
    h->setWordWrap(true);
    l->addWidget(h);
    logLine(QStringLiteral("Новый чат."));
}

void MainWindow::onSettings() {
    QString oldRoot = cfg_.projectDir;
    SettingsDialog dlg(cfg_, this); // конструктор сам делает exec() и сохраняет cfg_
    ds_->setCfg(cfg_);
    agent_->setCfg(cfg_);
    if (oldRoot != cfg_.projectDir) {
        agent_->setProjectRoot(cfg_.projectDir);
        rebuildTreeRoot();
    }
    logLine(QStringLiteral("Настройки применены."));
}

void MainWindow::onPickFolder() {
    QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка проекта"), cfg_.projectDir);
    if (d.isEmpty()) return;
    cfg_.projectDir = d;
    Cfg::save(cfg_);
    agent_->setProjectRoot(d);
    rebuildTreeRoot();
    logLine(QStringLiteral("Папка проекта: %1").arg(d));
}
