#include "mainwindow.h"
#include "deepseekclient.h"
#include "agent.h"
#include "squirclecard.h"
#include "sitepreview.h"

#include <QScrollArea>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QDockWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QApplication>
#include <QFileInfo>
#include <QFile>
#include <QTextOption>
#include <QScrollBar>
#include <QRegularExpression>
#include <QDir>
#include <QPixmap>
#include <QTimer>
#include <QTextDocument>
#include <QGraphicsOpacityEffect>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QStyle>
#include <QEasingCurve>
#include <QAbstractAnimation>
#include <QStandardPaths>
#include <QProcess>
#include <QCloseEvent>
#include <QDateTime>

// ============================================================================
//  тема: фон почти чёрный, акцент фиолетовый (как у популярных AI-IDE)
// ============================================================================
static const char* kTheme = R"(
QMainWindow, QDialog { background:#0a0c11; }
QWidget { color:#e9ecf6; font-size:13px; font-family:'Segoe UI','Inter',sans-serif; background:transparent; }
*[themeRoot="true"] { background:#0a0c11; }

QFrame#sidePanel { background:#0e1119; border:none; border-right:1px solid #1b2030; }
QLabel#appName { color:#f2f4ff; font-size:16px; font-weight:700; }
QLabel#appTag { color:#4c5476; font-size:11px; }
QLabel#sideHeader { color:#4c5476; font-size:10px; font-weight:700; letter-spacing:2px; padding:0 4px; }

QPushButton#newSession {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #6d5cff, stop:1 #8a5cf6);
  border:none; border-radius:14px; padding:12px; color:#ffffff; font-weight:700; font-size:13px;
}
QPushButton#newSession:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #7d6bff, stop:1 #9d6fff); }
QPushButton#newSession:pressed { background:#5848e0; }

QPushButton#ghostBtn {
  background:transparent; border:1px solid #232941; border-radius:10px; padding:8px 12px; color:#9aa1c2;
}
QPushButton#ghostBtn:hover { background:#181c2b; color:#dfe3f5; }

QListWidget#sessions { background:transparent; border:none; outline:none; }
QListWidget#sessions::item { padding:10px 12px; border-radius:12px; margin:2px 4px; color:#c4c9e0; }
QListWidget#sessions::item:hover { background:#151927; }
QListWidget#sessions::item:selected { background:#20264a; color:#ffffff; }

QLabel#sessTitle { color:#f2f4ff; font-size:17px; font-weight:700; }
QFrame#divider { background:#1b2030; max-height:1px; min-height:1px; }

QPushButton#chip {
  background:#141827; border:1px solid #262b47; border-radius:14px; padding:6px 14px; color:#8b91b3; font-size:12px;
}
QPushButton#chip:hover { border-color:#3d4470; color:#c6cbdf; }
QPushButton#chip:checked { background:#242a52; border-color:#6d5cff; color:#d9d4ff; }

QPushButton#sendBtn {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #6d5cff, stop:1 #a06bff);
  border:none; border-radius:16px; min-width:44px; min-height:44px; max-width:44px; max-height:44px;
  color:#ffffff; font-weight:700; font-size:16px;
}
QPushButton#sendBtn:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8270ff, stop:1 #b58aff); }
QPushButton#sendBtn:pressed { background:#5848e0; }
QPushButton#sendBtn:!enabled { background:#241d3b; color:#5a5290; }

/* busy: превращается в Стоп (круглая красная) */
QPushButton#sendBtn[busy="true"] {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #d4506a, stop:1 #a83a56);
}

QPlainTextEdit#composer {
  background:#11151f; border:1px solid #232941; border-radius:18px; padding:14px 18px;
  selection-background-color:#3c4180; selection-color:#ffffff;
}
QPlainTextEdit#composer:focus { border:1px solid #4a52a8; }
QPlainTextEdit#composer:disabled { background:#0e1119; color:#4c5476; }

QPlainTextEdit#logView { font-family:'Cascadia Code','Consolas',monospace; font-size:12px; color:#8d93af; }

QDockWidget { color:#697093; font-weight:600; }
QDockWidget::title { background:#0d1017; padding:9px 14px; border-top:1px solid #1b2030; }

QSplitter::handle { background:transparent; }
QSplitter::handle:horizontal { width:2px; }
QSplitter::handle:horizontal:hover { background:#333a63; }

QScrollBar:vertical { background:transparent; width:10px; margin:4px 2px; }
QScrollBar::handle:vertical { background:#232a45; border-radius:5px; min-height:36px; }
QScrollBar::handle:vertical:hover { background:#3b4170; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar:horizontal { background:transparent; height:10px; margin:2px 4px; }
QScrollBar::handle:horizontal { background:#232a45; border-radius:5px; min-width:36px; }
QScrollBar::handle:horizontal:hover { background:#3b4170; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
QScrollArea { border:none; background:transparent; }

QLineEdit, QSpinBox, QComboBox {
  background:#141827; border:1px solid #262b47; border-radius:8px; padding:7px 12px; min-height:20px;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color:#8d83ff; }
QComboBox QAbstractItemView { background:#151826; border:1px solid #262b42; selection-background-color:#2d3253; }
QComboBox::drop-down { border:none; width:24px; }
QCheckBox { spacing:8px; }
QCheckBox::indicator { width:16px; height:16px; border-radius:5px; border:1px solid #3a4166; background:#141827; }
QCheckBox::indicator:checked { background:#6d5cff; border-color:#6d5cff; }
QToolTip { background:#1b1e30; color:#e9ecf6; border:1px solid #2e3354; padding:7px 10px; border-radius:8px; }
)";

// ============================================================================
MainWindow::MainWindow() {
    cfg_ = Cfg::load();
    setWindowTitle(QStringLiteral("DeepSeekIDE"));
    resize(1280, 840);
    setMinimumSize(980, 620);
    qApp->setStyleSheet(QLatin1String(kTheme));

    ds_ = new DeepSeekClient(this);
    ds_->setCfg(cfg_);
    agent_ = new Agent(ds_, this);
    agent_->setCfg(cfg_);

    // ==================== левая панель: лого + сессии ====================
    auto* side = new QFrame; side->setObjectName(QStringLiteral("sidePanel"));
    side->setMinimumWidth(264); side->setMaximumWidth(264);
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(16, 18, 16, 14);
    sl->setSpacing(10);

    auto* logoRow = new QHBoxLayout; logoRow->setSpacing(10);
    logoLbl_ = new QLabel;
    QPixmap logo(QStringLiteral("assets/logo.png"));
    if (logo.isNull()) { // фолбэк: рисуем мини-скверкл программно
        logo = QPixmap(44, 44); logo.fill(Qt::transparent);
        QPainter p(&logo); p.setRenderHint(QPainter::Antialiasing);
        QPainterPath sp = SquircleCard::squirclePath(QRectF(1, 1, 42, 42), 4.6);
        QLinearGradient g(0, 0, 44, 44);
        g.setColorAt(0, QColor(109, 92, 255)); g.setColorAt(1, QColor(160, 107, 255));
        p.fillPath(sp, g);
        p.end();
    }
    logoLbl_->setPixmap(logo.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoRow->addWidget(logoLbl_);
    auto* nameCol = new QVBoxLayout; nameCol->setSpacing(1);
    auto* appName = new QLabel(QStringLiteral("DeepSeekIDE")); appName->setObjectName(QStringLiteral("appName"));
    auto* appTag = new QLabel(QStringLiteral("ваш код — его задача")); appTag->setObjectName(QStringLiteral("appTag"));
    nameCol->addWidget(appName); nameCol->addWidget(appTag);
    logoRow->addLayout(nameCol);
    logoRow->addStretch(1);
    sl->addLayout(logoRow);

    auto* newBtn = new QPushButton(QStringLiteral("+  Новая сессия"));
    newBtn->setObjectName(QStringLiteral("newSession"));
    newBtn->setCursor(Qt::PointingHandCursor);
    connect(newBtn, &QPushButton::clicked, this, &MainWindow::onNewSession);
    sl->addWidget(newBtn);

    auto* sideHdr = new QLabel(QStringLiteral("СЕССИИ")); sideHdr->setObjectName(QStringLiteral("sideHeader"));
    sl->addWidget(sideHdr);

    sessionList_ = new QListWidget; sessionList_->setObjectName(QStringLiteral("sessions"));
    sessionList_->setIconSize(QSize(0, 0));
    connect(sessionList_, &QListWidget::currentRowChanged, this, &MainWindow::onPickSession);
    sl->addWidget(sessionList_, 1);

    auto* bottom = new QVBoxLayout; bottom->setSpacing(8);
    auto* sbxBtn = new QPushButton(QStringLiteral("Песочница"))
        ; sbxBtn->setObjectName(QStringLiteral("ghostBtn"));
    sbxBtn->setCursor(Qt::PointingHandCursor);
    connect(sbxBtn, &QPushButton::clicked, this, &MainWindow::onOpenSandbox);
    auto* setBtn = new QPushButton(QStringLiteral("Настройки…")); setBtn->setObjectName(QStringLiteral("ghostBtn"));
    setBtn->setCursor(Qt::PointingHandCursor);
    connect(setBtn, &QPushButton::clicked, this, &MainWindow::onSettings);
    bottom->addWidget(sbxBtn);
    bottom->addWidget(setBtn);
    sl->addLayout(bottom);

    // ==================== центр: чат ====================
    auto* center = new QWidget;
    center->setProperty("themeRoot", true);
    auto* cl = new QVBoxLayout(center);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    // шапка сессии
    auto* head = new QWidget;
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(26, 18, 26, 14);
    sessionTitle_ = new QLabel(QStringLiteral("DeepSeekIDE"));
    sessionTitle_->setObjectName(QStringLiteral("sessTitle"));
    hl->addWidget(sessionTitle_, 0);
    statusPill_ = new QLabel(QStringLiteral("• готов"));
    statusPill_->setStyleSheet(QStringLiteral("color:#35cf7f; font-size:12px; padding-left:10px;"));
    hl->addWidget(statusPill_, 0);
    hl->addStretch(1);
    cl->addWidget(head);
    auto* div = new QFrame; div->setObjectName(QStringLiteral("divider"));
    cl->addWidget(div);

    // поток чата
    chatScroll_ = new QScrollArea;
    chatScroll_->setWidgetResizable(true);
    auto* chatHost = new QWidget;
    chatLay_ = new QVBoxLayout(chatHost);
    chatLay_->setContentsMargins(0, 18, 0, 18);
    chatLay_->setSpacing(14);
    chatLay_->addStretch(1);
    chatScroll_->setWidget(chatHost);
    cl->addWidget(chatScroll_, 1);

    // нижняя зона: чипы + композер
    auto* bottomWrap = new QWidget;
    auto* bw = new QVBoxLayout(bottomWrap);
    bw->setContentsMargins(26, 6, 26, 22);
    bw->setSpacing(10);

    auto* chipsRow = new QHBoxLayout; chipsRow->setSpacing(8);
    chipThink_  = new QPushButton(QStringLiteral("DeepThink"));  chipThink_->setObjectName(QStringLiteral("chip"));
    chipSearch_ = new QPushButton(QStringLiteral("Поиск"));      chipSearch_->setObjectName(QStringLiteral("chip"));
    chipImg_    = new QPushButton(QStringLiteral("Картинки"));   chipImg_->setObjectName(QStringLiteral("chip"));
    for (auto* c : {chipThink_, chipSearch_, chipImg_}) { c->setCheckable(true); c->setCursor(Qt::PointingHandCursor); }
    chipThink_->setChecked(cfg_.thinking);
    chipSearch_->setChecked(cfg_.search);
    chipImg_->setChecked(cfg_.imgOn);
    connect(chipThink_, &QPushButton::toggled, this, [this](bool on){ cfg_.thinking = on; Cfg::save(cfg_); ds_->setCfg(cfg_); });
    connect(chipSearch_, &QPushButton::toggled, this, [this](bool on){ cfg_.search = on; Cfg::save(cfg_); ds_->setCfg(cfg_); });
    connect(chipImg_, &QPushButton::toggled, this, [this](bool on){ cfg_.imgOn = on; Cfg::save(cfg_); agent_->setCfg(cfg_); });

    chipsRow->addWidget(chipThink_);
    chipsRow->addWidget(chipSearch_);
    chipsRow->addWidget(chipImg_);
    chipsRow->addStretch(1);

    folderPill_ = new QPushButton(QStringLiteral("папка не выбрана"));
    folderPill_->setObjectName(QStringLiteral("chip"));
    folderPill_->setCursor(Qt::PointingHandCursor);
    connect(folderPill_, &QPushButton::clicked, this, &MainWindow::onPickFolder);
    chipsRow->addWidget(folderPill_);
    bw->addLayout(chipsRow);

    auto* inputRow = new QHBoxLayout; inputRow->setSpacing(12);
    input_ = new QPlainTextEdit; input_->setObjectName(QStringLiteral("composer"));
    input_->setPlaceholderText(QStringLiteral("Опишите задачу или задайте вопрос…"));
    input_->setMinimumHeight(58);
    input_->setMaximumHeight(150);
    input_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Return)), this, SLOT(onSend()));
    new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Enter)), this, SLOT(onSend()));
    sendBtn_ = new QPushButton(QStringLiteral("↑"));
    sendBtn_->setObjectName(QStringLiteral("sendBtn"));
    sendBtn_->setCursor(Qt::PointingHandCursor);
    sendBtn_->setToolTip(QStringLiteral("Отправить (Ctrl+Enter)"));
    connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::onSend);
    inputRow->addWidget(input_, 1);
    inputRow->addWidget(sendBtn_, 0, Qt::AlignBottom);
    bw->addLayout(inputRow);
    cl->addWidget(bottomWrap);

    // ==================== сплит ====================
    auto* split = new QSplitter;
    split->addWidget(side);
    split->addWidget(center);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setCollapsible(0, false);
    split->setSizes({264, 1000});

    auto* central = new QWidget;
    central->setProperty("themeRoot", true);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(split);
    setCentralWidget(central);

    // лог-док (по умолчанию скрыт)
    logView_ = new QPlainTextEdit; logView_->setObjectName(QStringLiteral("logView"));
    logView_->setReadOnly(true); logView_->setMaximumBlockCount(2000);
    auto* logDock = new QDockWidget(QStringLiteral("ЛОГ"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    logDock->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->setMinimumHeight(120);
    logDock->hide();
    auto* logSc = new QShortcut(QKeySequence(Qt::Key_F10), this);
    connect(logSc, &QShortcut::activated, this, [logDock]() { logDock->setVisible(!logDock->isVisible()); });

    // ==================== вайринг агента ====================
    connect(agent_, &Agent::appendUserBubble, this, &MainWindow::addUser);
    connect(agent_, &Agent::assistantBubbleStart, this, &MainWindow::beginAssistant);
    connect(agent_, &Agent::assistantDelta, this, &MainWindow::appendAssistantDelta);
    connect(agent_, &Agent::assistantBubbleDone, this, &MainWindow::endAssistant);
    connect(agent_, &Agent::toolRow, this, &MainWindow::addToolRow);
    connect(agent_, &Agent::imageShown, this, &MainWindow::addImage);
    connect(agent_, &Agent::errorBanner, this, &MainWindow::addError);
    connect(agent_, &Agent::sitePreview, this, &MainWindow::showSitePreview);
    connect(agent_, &Agent::busyChanged, this, &MainWindow::onBusy);
    connect(agent_, &Agent::log, this, &MainWindow::logLine);
    connect(ds_, &DeepSeekClient::log, this, &MainWindow::logLine);

    connect(&pulseTimer_, &QTimer::timeout, this, &MainWindow::pulseTick);
    pulseTimer_.setInterval(150);

    // ==================== сессии ====================
    sessions_ = SessionStore().load();
    refreshSessionList();
    if (sessions_.isEmpty()) {
        // первая сессия — из cfg_.projectDir
        Session s = Session::create(cfg_.projectDir);
        sessions_.prepend(s);
        persistNow();
        refreshSessionList();
    }
    activateSession(0);

    logLine(cfg_.token.isEmpty()
        ? QStringLiteral("Токен не задан. Настройки (слева внизу) → DeepSeek token.")
        : QStringLiteral("Готово. Опишите задачу."));
}

// ============================================================================
//  карточки чата: скверкл-обёртки, выравнивание в центр-контейнере
// ============================================================================
SquircleCard* MainWindow::chatAppend(const QString& role, const QString& state, bool animate) {
    auto* card = new SquircleCard(role);
    if (!state.isEmpty()) card->setState(state);
    auto* l = new QVBoxLayout(card);
    l->setContentsMargins(18, 13, 18, 13);
    l->setSpacing(6);

    auto* wrap = new QWidget;
    auto* hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(26, 0, 26, 0);
    if (role == QStringLiteral("user")) {
        hl->addStretch(2);
        card->setMaximumWidth(680);
        hl->addWidget(card, 8);
    } else {
        card->setMaximumWidth(820);
        hl->addWidget(card, 8);
        hl->addStretch(2);
    }
    chatLay_->insertWidget(chatLay_->count() - 1, wrap);
    if (animate) fadeIn(wrap);
    QTimer::singleShot(0, this, [this] { smoothScrollBottom(); });
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
    anim->setDuration(200);
    anim->setStartValue(sb->value());
    anim->setEndValue(sb->maximum());
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::pulseTick() {
    if (!liveCard_) return;
    pulsePhase_ = (pulsePhase_ + 1) % 4;
    static const QColor cols[4] = {
        QColor(141, 131, 255), QColor(168, 159, 255),
        QColor(195, 188, 255), QColor(168, 159, 255) };
    liveCard_->setPulseColor(cols[pulsePhase_]);
}

QString MainWindow::mdToHtml(const QString& md) {
    QString s = md.toHtmlEscaped();
    s.replace(QRegularExpression(QStringLiteral("```(\\w*)\\n([\\s\\S]*?)```")),
              QStringLiteral("<pre style='background:#080a10;border:1px solid #232941;border-radius:10px;padding:12px;font-family:Consolas,monospace;color:#c9d3f4;'>\\2</pre>"));
    s.replace(QRegularExpression(QStringLiteral("`([^`\\n]+)`")),
              QStringLiteral("<code style='background:#0b0d14;border:1px solid #232941;border-radius:5px;padding:1px 7px;font-family:Consolas,monospace;color:#b9aaff;'>\\1</code>"));
    s.replace(QRegularExpression(QStringLiteral("^####?\\s+(.+)$"), QRegularExpression::MultilineOption),
              QStringLiteral("<b style='color:#a298ff;font-size:14px'>\\1</b>"));
    s.replace(QRegularExpression(QStringLiteral("\\*\\*([^*\\n]+)\\*\\*")),
              QStringLiteral("<b>\\1</b>"));
    s.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return s;
}

// ============================================================================
//  сессии
// ============================================================================
void MainWindow::persistNow() {
    if (curSess_ >= 0 && curSess_ < sessions_.size()) {
        sessions_[curSess_].dsSessionId = ds_->sessionId();
        sessions_[curSess_].parentMessageId = ds_->parentMessageId();
    }
    SessionStore().save(sessions_);
}

void MainWindow::refreshSessionList() {
    sessionList_->blockSignals(true);
    sessionList_->clear();
    for (const Session& s : sessions_) {
        auto* it = new QListWidgetItem(s.title.left(38) + (s.title.size() > 38 ? QStringLiteral("…") : QString()));
        it->setToolTip(QStringLiteral("%1\n%2").arg(s.title, s.folder));
        sessionList_->addItem(it);
    }
    sessionList_->blockSignals(false);
    if (curSess_ >= 0 && curSess_ < sessionList_->count())
        sessionList_->setCurrentRow(curSess_);
}

void MainWindow::activateSession(int i) {
    if (i < 0 || i >= sessions_.size()) return;
    curSess_ = i;
    sessionList_->setCurrentRow(i);
    const Session& s = sessions_[i];
    sessionTitle_->setText(s.title.left(56));
    folderPill_->setText(s.folder.isEmpty() ? QStringLiteral("папка не выбрана") : QFileInfo(s.folder).fileName());
    folderPill_->setToolTip(s.folder);

    clearChatUi();
    replaying_ = true;
    replayHistory();
    replaying_ = false;
    if (s.history.isEmpty()) showEmptyState();

    ds_->setSession(s.dsSessionId, s.parentMessageId);
    agent_->setProjectRoot(s.folder);
    agent_->restoreSession(!s.dsSessionId.isEmpty() || !s.history.isEmpty());
    if (!cfg_.projectDir.isEmpty()) logLine(QStringLiteral("Сессия: %1").arg(s.title));
}

void MainWindow::onPickSession(int row) {
    if (busyOn_ || row == curSess_ || row < 0 || row >= sessions_.size()) return;
    persistNow();
    activateSession(row);
}

void MainWindow::onNewSession() {
    if (busyOn_) return;
    persistNow();
    Session s = Session::create(curSess_ >= 0 ? sessions_[curSess_].folder : cfg_.projectDir);
    sessions_.prepend(s);
    persistNow();
    curSess_ = 0;
    refreshSessionList();
    activateSession(0);
}

void MainWindow::setSessionTitleFromText(const QString& t) {
    if (curSess_ < 0) return;
    Session& s = sessions_[curSess_];
    if (!s.title.isEmpty() && s.title != QStringLiteral("Новая сессия")) return;
    s.title = t.simplified().left(48);
    if (s.title.isEmpty()) s.title = QStringLiteral("Сессия");
    sessionTitle_->setText(s.title.left(56));
    refreshSessionList();
    persistNow();
}

void MainWindow::clearChatUi() {
    pulseTimer_.stop();
    liveCard_ = nullptr; liveBubble_ = nullptr; liveRaw_.clear();
    for (int i = chatLay_->count() - 2; i >= 0; --i) {
        QLayoutItem* it = chatLay_->itemAt(i);
        if (!it) continue;
        delete it->widget();
        chatLay_->removeItem(it);
    }
}

void MainWindow::showEmptyState() {
    liveCard_ = nullptr;
    auto* wrap = new QWidget;
    auto* vl = new QVBoxLayout(wrap);
    vl->setAlignment(Qt::AlignCenter);
    auto* big = new QLabel(QStringLiteral("Что будем строить?"));
    big->setStyleSheet(QStringLiteral("color:#f2f4ff; font-size:26px; font-weight:700;"));
    big->setAlignment(Qt::AlignCenter);
    auto* sub = new QLabel(QStringLiteral(
        "Агент читает и правит файлы проекта, выполняет команды,\n"
        "ходит в интернет, генерирует картинки и сам всё собирает."));
    sub->setStyleSheet(QStringLiteral("color:#4c5476; font-size:14px;"));
    sub->setAlignment(Qt::AlignCenter);
    vl->addWidget(big);
    vl->addSpacing(8);
    vl->addWidget(sub);
    chatLay_->insertWidget(chatLay_->count() - 1, wrap);
}

void MainWindow::pushHist(const QString& role, const QString& text, const QString& name,
                          const QString& sub, bool ok, const QString& detail) {
    if (replaying_ || curSess_ < 0 || curSess_ >= sessions_.size()) return;
    QJsonObject e{{QStringLiteral("role"), role}};
    if (!text.isEmpty()) e[QStringLiteral("text")] = text;
    if (!name.isEmpty()) e[QStringLiteral("name")] = name;
    if (!sub.isEmpty()) e[QStringLiteral("sub")] = sub;
    e[QStringLiteral("ok")] = ok;
    if (!detail.isEmpty()) e[QStringLiteral("detail")] = detail;
    sessions_[curSess_].history.append(e);
    if (sessions_[curSess_].history.size() > 400) sessions_[curSess_].history.removeFirst();
    persistNow();
}

void MainWindow::replayHistory() {
    if (curSess_ < 0 || curSess_ >= sessions_.size()) return;
    for (const QJsonValue& v : sessions_[curSess_].history) {
        const QJsonObject e = v.toObject();
        const QString role = e[QStringLiteral("role")].toString();
        if (role == QLatin1String("user")) {
            auto* c = chatAppend(QStringLiteral("user"), QString(), false);
            auto* l = qobject_cast<QVBoxLayout*>(c->layout());
            auto* lbl = new QLabel(mdToHtml(e[QStringLiteral("text")].toString()));
            lbl->setWordWrap(true); l->addWidget(lbl);
        } else if (role == QLatin1String("assistant")) {
            auto* c = chatAppend(QStringLiteral("assistant"), QString(), false);
            auto* l = qobject_cast<QVBoxLayout*>(c->layout());
            auto* lbl = new QLabel(mdToHtml(e[QStringLiteral("text")].toString()));
            lbl->setWordWrap(true); lbl->setTextInteractionFlags(Qt::TextSelectableByMouse); l->addWidget(lbl);
        } else if (role == QLatin1String("tool")) {
            addToolRow(e[QStringLiteral("name")].toString(), e[QStringLiteral("sub")].toString(),
                       e[QStringLiteral("ok")].toBool(true), QString());
        } else if (role == QLatin1String("error")) {
            auto* c = chatAppend(QStringLiteral("error"), QString(), false);
            auto* l = qobject_cast<QVBoxLayout*>(c->layout());
            auto* lbl = new QLabel(QStringLiteral("<span style='color:#ffb7c5'>%1</span>")
                                       .arg(e[QStringLiteral("text")].toString().toHtmlEscaped()));
            lbl->setWordWrap(true); l->addWidget(lbl);
        }
    }
}

// ============================================================================
//  сообщения чата
// ============================================================================
void MainWindow::onSend() {
    if (busyOn_) { ds_->abort(); return; }
    if (curSess_ >= 0 && sessions_[curSess_].folder.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Папка проекта"),
            QStringLiteral("Сначала укажите папку проекта для этой сессии (кнопка справа от чипов)."));
        onPickFolder();
        if (curSess_ >= 0 && sessions_[curSess_].folder.isEmpty()) return;
    }
    QString t = input_->toPlainText().trimmed();
    if (t.isEmpty()) return;
    input_->clear();
    setSessionTitleFromText(t);
    agent_->sendUserMessage(t);
}

void MainWindow::onPickFolder() {
    if (curSess_ < 0) return;
    QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка проекта"),
        sessions_[curSess_].folder.isEmpty() ? cfg_.projectDir : sessions_[curSess_].folder);
    if (d.isEmpty()) return;
    sessions_[curSess_].folder = d;
    agent_->setProjectRoot(d);
    folderPill_->setText(QFileInfo(d).fileName());
    folderPill_->setToolTip(d);
    persistNow();
    logLine(QStringLiteral("Папка сессии: %1").arg(d));
}

void MainWindow::onBusy(bool on) {
    busyOn_ = on;
    sendBtn_->setProperty("busy", on);
    sendBtn_->style()->unpolish(sendBtn_);
    sendBtn_->style()->polish(sendBtn_);
    sendBtn_->setText(on ? QStringLiteral("×") : QStringLiteral("↑"));
    sendBtn_->setToolTip(on ? QStringLiteral("Остановить") : QStringLiteral("Отправить (Ctrl+Enter)"));
    input_->setDisabled(on);
    input_->setPlaceholderText(on ? QStringLiteral("Агент пишет/работает…")
                                  : QStringLiteral("Опишите задачу или задайте вопрос…"));
    statusPill_->setText(on ? QStringLiteral("• работает") : QStringLiteral("• готов"));
    statusPill_->setStyleSheet(on
        ? QStringLiteral("color:#8d83ff; font-size:12px; padding-left:10px;")
        : QStringLiteral("color:#35cf7f; font-size:12px; padding-left:10px;"));
}

void MainWindow::addUser(const QString& text) {
    auto* c = chatAppend(QStringLiteral("user"));
    auto* l = qobject_cast<QVBoxLayout*>(c->layout());
    auto* lbl = new QLabel(mdToHtml(text));
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QStringLiteral("color:#eef0ff;"));
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(lbl);
    pushHist(QStringLiteral("user"), text);
}

void MainWindow::beginAssistant() {
    liveCard_ = chatAppend(QStringLiteral("assistant"), QStringLiteral("live"));
    auto* l = qobject_cast<QVBoxLayout*>(liveCard_->layout());
    liveBubble_ = new QLabel(QStringLiteral("<span style='color:#4c5476;font-style:italic'>печатает…</span>"));
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
            ? QStringLiteral("<span style='color:#4c5476;font-style:italic'>…продолжаю работу —</span>")
            : mdToHtml(cleanText));
    }
    if (liveCard_) {
        liveCard_->setState(QString()); // снять live
        liveCard_->setPulseColor(QColor());
        liveCard_ = nullptr;
        liveBubble_ = nullptr;
        if (!cleanText.isEmpty()) pushHist(QStringLiteral("assistant"), cleanText);
        liveRaw_.clear();
    }
    smoothScrollBottom();
}

void MainWindow::addToolRow(const QString& name, const QString& sub, bool ok, const QString& detail) {
    auto* card = chatAppend(QStringLiteral("tool"), ok ? QString() : QStringLiteral("fail"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(QStringLiteral("<b style='color:%1'>%2</b> <span style='color:#697093'>%3</span>")
                               .arg(ok ? QStringLiteral("#35cf7f") : QStringLiteral("#ff5470"),
                                    name.toHtmlEscaped(), sub.toHtmlEscaped()));
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QStringLiteral("font-family:'Cascadia Code','Consolas',monospace;font-size:12px;"));
    l->addWidget(lbl);
    if (!detail.isEmpty()) {
        auto* det = new QLabel(QStringLiteral("<pre style='color:#767d9e;font-size:11px;font-family:Consolas,monospace;'>%1</pre>")
                                   .arg(detail.left(1500).toHtmlEscaped()));
        det->setWordWrap(true);
        l->addWidget(det);
    }
    pushHist(QStringLiteral("tool"), QString(), name, sub, ok, detail.left(400));
}

void MainWindow::addImage(const QByteArray& bytes, const QString& mime, const QString& alt) {
    Q_UNUSED(mime);
    QPixmap pm;
    if (!pm.loadFromData(bytes)) { addError(QStringLiteral("Картинка не декодирована")); return; }
    auto* card = chatAppend(QStringLiteral("assistant"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel;
    lbl->setPixmap(pm.scaledToWidth(240, Qt::SmoothTransformation));
    lbl->setToolTip(alt.isEmpty() ? QStringLiteral("image") : alt);
    l->addWidget(lbl);
}

void MainWindow::addError(const QString& err) {
    pulseTimer_.stop();
    auto* card = chatAppend(QStringLiteral("error"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(QStringLiteral("<b style='color:#ff5470'>Ошибка</b><br><span style='color:#ffb7c5'>%1</span>")
                               .arg(err.toHtmlEscaped()));
    lbl->setWordWrap(true);
    l->addWidget(lbl);
    pushHist(QStringLiteral("error"), err);
}

void MainWindow::showSitePreview(const QString& url, const QString& html) {
    auto* d = new SitePreviewDialog(url, html, this);
    d->show();
    d->raise();
}

void MainWindow::logLine(const QString& line) {
    logView_->appendPlainText(line);
}

void MainWindow::onOpenSandbox() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/sandbox");
    QDir().mkpath(dir);
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList{ QDir::toNativeSeparators(dir) });
#endif
}

void MainWindow::onSettings() {
    SettingsDialog dlg(cfg_, this);
    ds_->setCfg(cfg_);
    agent_->setCfg(cfg_);
    chipThink_->setChecked(cfg_.thinking);
    chipSearch_->setChecked(cfg_.search);
    chipImg_->setChecked(cfg_.imgOn);
    logLine(QStringLiteral("Настройки применены."));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    persistNow();
    QMainWindow::closeEvent(e);
}
