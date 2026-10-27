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

static const char* kDark = R"(
QMainWindow, QWidget { background:#1b1b1f; color:#e7e7ea; font-size:13px; }
QTreeView, QTabWidget::pane { background:#222228; border:1px solid #33333c; }
QHeaderView::section { background:#26262c; color:#cfcfd6; border:none; padding:4px; }
QPlainTextEdit, QScrollArea > QWidget > QWidget { background:#222228; border:1px solid #33333c; }
QPushButton { background:#3a3a44; border:1px solid #4a4a55; border-radius:4px; padding:6px 14px; }
QPushButton:hover { background:#454550; }
QPushButton:disabled { background:#2a2a30; color:#777; }
QTabBar::tab { background:#26262c; padding:6px 12px; border:1px solid #33333c; border-bottom:none; }
QTabBar::tab:selected { background:#1b1b1f; color:#fff; }
QDockWidget::title { background:#26262c; padding:4px; }
)";

MainWindow::MainWindow() {
    cfg_ = Cfg::load();
    setWindowTitle(QStringLiteral("DeepSeek C++ IDE"));
    resize(1400, 900);
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

    // левая колонка: дерево
    fsModel_ = new QFileSystemModel(this);
    fsModel_->setReadOnly(true);
    fsModel_->setNameFilters(QStringList{});
    tree_ = new QTreeView;
    tree_->setModel(fsModel_);
    tree_->setHeaderHidden(true);
    tree_->setAnimated(false);
    for (int i = 1; i < 4; ++i) tree_->hideColumn(i);
    connect(tree_, &QTreeView::doubleClicked, this, &MainWindow::onTreeOpen);

    // центр: табы редакторов
    tabs_ = new QTabWidget;
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabClose);

    // правая колонка: чат
    chatScroll_ = new QScrollArea;
    chatScroll_->setWidgetResizable(true);
    auto* chatHost = new QWidget;
    chatLay_ = new QVBoxLayout(chatHost);
    chatLay_->setContentsMargins(8, 8, 8, 8);
    chatLay_->setSpacing(8);
    chatLay_->addStretch(1);
    chatScroll_->setWidget(chatHost);

    input_ = new QPlainTextEdit;
    input_->setPlaceholderText(QStringLiteral("Сообщение (Ctrl+Enter — отправить)…"));
    input_->setMaximumHeight(90);
    input_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Return)), this, SLOT(onSend()));
    sendBtn_ = new QPushButton(QStringLiteral("Отправить"));
    connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::onSend);

    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(6, 6, 6, 6);
    rl->setSpacing(6);
    rl->addWidget(chatScroll_, 1);
    auto* inputRow = new QWidget;
    auto* irl = new QHBoxLayout(inputRow);
    irl->setContentsMargins(0, 0, 0, 0);
    irl->addWidget(input_, 1);
    irl->addWidget(sendBtn_);
    rl->addWidget(inputRow);

    auto* mainSplit = new QSplitter;
    mainSplit->addWidget(tree_);
    mainSplit->addWidget(tabs_);
    mainSplit->addWidget(right);
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setStretchFactor(2, 0);
    mainSplit->setSizes({260, 760, 420});

    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(2000);
    auto* logDock = new QDockWidget(QStringLiteral("Лог"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    logDock->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->setMinimumHeight(110);

    auto* central = new QWidget;
    auto* cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(mainSplit);
    setCentralWidget(central);

    rebuildTreeRoot();

    // вайринг агента
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

    logLine(cfg_.token.isEmpty()
        ? QStringLiteral("Токен не задан. Откройте Настройки и вставьте Bearer-токен DeepSeek.")
        : QStringLiteral("Готово. Пишите сообщение."));
}

// ---------- базовые UI-помощники ----------
QWidget* MainWindow::chatAddCard(const QString& bg, const QString& fg) {
    auto* f = new QFrame;
    f->setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid #33333c;border-radius:8px;} QLabel{background:transparent;color:%2;}")
                         .arg(bg, fg.isEmpty() ? QStringLiteral("#e7e7ea") : fg));
    auto* l = new QVBoxLayout(f);
    l->setContentsMargins(10, 8, 10, 8);
    chatLay_->insertWidget(chatLay_->count() - 1, f);
    return f;
}

QString MainWindow::mdToHtml(const QString& md) {
    QString s = md.toHtmlEscaped();
    // кодовые заборы
    s.replace(QRegularExpression(QStringLiteral("```(\\w*)\\n([\\s\\S]*?)```")),
              QStringLiteral("<pre style='background:#16161a;border:1px solid #333;padding:8px;'><code>\\2</code></pre>"));
    s.replace(QRegularExpression(QStringLiteral("`([^`\n]+)`")),
              QStringLiteral("<code style='background:#16161a;padding:1px 4px;'>\\1</code>"));
    s.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return s;
}

// ---------- файлы ----------
void MainWindow::rebuildTreeRoot() {
    QString root = fsModel_->setRootPath(cfg_.projectDir.isEmpty() ? QDir::currentPath() : cfg_.projectDir);
    tree_->setRootIndex(fsModel_->index(root));
    setWindowTitle(QStringLiteral("DeepSeek C++ IDE — %1").arg(QDir(root).dirName()));
}

void MainWindow::onTreeOpen(const QModelIndex& idx) {
    if (fsModel_->isDir(idx)) return;
    openFile(fsModel_->filePath(idx));
}

CodeEditor* MainWindow::openFile(const QString& absPath) {
    // уже открыт?
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

// ---------- чат-UI ----------
void MainWindow::onSend() {
    QString t = input_->toPlainText().trimmed();
    if (t.isEmpty()) return;
    input_->clear();
    agent_->sendUserMessage(t);
}

void MainWindow::onBusy(bool on) {
    sendBtn_->setEnabled(!on);
    input_->setDisabled(on);
}

void MainWindow::addUser(const QString& text) {
    auto* card = chatAddCard(QStringLiteral("#2b2b34"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(mdToHtml(text));
    lbl->setWordWrap(true);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(lbl);
}

void MainWindow::beginAssistant() {
    auto* card = chatAddCard(QStringLiteral("#23232a"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    liveBubble_ = new QLabel;
    liveBubble_->setWordWrap(true);
    liveBubble_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(liveBubble_);
    liveRaw_.clear();
}

void MainWindow::appendAssistantDelta(const QString& delta) {
    if (!liveBubble_) beginAssistant();
    liveRaw_ += delta;
    liveBubble_->setText(liveRaw_); // вживую — сырой plain; финальный — HTML
    QScrollBar* sb = chatScroll_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::endAssistant(const QString& cleanText) {
    if (liveBubble_) {
        liveBubble_->setText(mdToHtml(cleanText));
        liveBubble_ = nullptr;
        liveRaw_.clear();
    }
    QScrollBar* sb = chatScroll_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::addToolRow(const QString& name, const QString& sub, bool ok, const QString& detail) {
    auto* card = chatAddCard(QStringLiteral("#16161c"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(QStringLiteral("<b style='color:%1'>%2</b> <span style='color:#8aa'>%3</span>")
                               .arg(ok ? QStringLiteral("#5fb875") : QStringLiteral("#e06c75"),
                                    name.toHtmlEscaped(), sub.toHtmlEscaped()));
    lbl->setWordWrap(true);
    l->addWidget(lbl);
    if (!detail.isEmpty()) {
        auto* det = new QLabel(QStringLiteral("<pre style='color:#a9a9b3;font-size:11px;'>%1</pre>")
                                   .arg(detail.left(1500).toHtmlEscaped()));
        det->setWordWrap(true);
        l->addWidget(det);
    }
    QScrollBar* sb = chatScroll_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::addImage(const QByteArray& bytes, const QString& mime, const QString& alt) {
    Q_UNUSED(mime);
    QPixmap pm;
    if (!pm.loadFromData(bytes)) { addError(QStringLiteral("Картинка не декодирована")); return; }
    auto* card = chatAddCard(QStringLiteral("#23232a"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel;
    lbl->setPixmap(pm.scaledToWidth(220, Qt::SmoothTransformation));
    lbl->setToolTip(alt.isEmpty() ? QStringLiteral("image") : alt);
    l->addWidget(lbl);
    QScrollBar* sb = chatScroll_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::addError(const QString& err) {
    auto* card = chatAddCard(QStringLiteral("#2a1c20"), QStringLiteral("#f0a0a8"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(QStringLiteral("<b>Ошибка</b><br>%1").arg(err.toHtmlEscaped()));
    lbl->setWordWrap(true);
    l->addWidget(lbl);
}

void MainWindow::logLine(const QString& line) {
    logView_->appendPlainText(line);
}

// ---------- верхние действия ----------
void MainWindow::onNewChat() {
    ds_->newChat();
    agent_->newChat();
    // очистить чат-лейаут
    liveBubble_ = nullptr; liveRaw_.clear();
    for (int i = chatLay_->count() - 2; i >= 0; --i) {
        QLayoutItem* it = chatLay_->itemAt(i);
        if (!it) continue;
        delete it->widget();
        chatLay_->removeItem(it);
    }
    auto* card = chatAddCard(QStringLiteral("#23232a"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    l->addWidget(new QLabel(QStringLiteral("Новый чат открыт.")));
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
}
