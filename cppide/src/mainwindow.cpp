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
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QDesktopServices>
#include <QUrl>
#include <QDialog>
#include <functional>

// ============================================================================
//  тема: фон почти чёрный, акцент фиолетовый (как у популярных AI-IDE)
// ============================================================================
//  тема: графитовый фон, бордовый акцент
// ============================================================================
static const char* kTheme = R"(
QMainWindow, QDialog { background:#0c0d0f; }
QWidget { color:#e6e7ea; font-size:13px; font-family:'Segoe UI','Inter',sans-serif; background:transparent; }
*[themeRoot="true"] { background:#0c0d0f; }

QFrame#sidePanel { background:#131417; border:none; border-right:1px solid #232428; }
QLabel#appName { color:#f4f5f7; font-size:16px; font-weight:700; }
QLabel#appTag { color:#6f727c; font-size:11px; }
QLabel#sideHeader { color:#575962; font-size:10px; font-weight:700; letter-spacing:2px; padding:0 4px; }

QPushButton#newSession {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #a6334d, stop:1 #7d2440);
  border:none; border-radius:14px; padding:12px; color:#ffffff; font-weight:700; font-size:13px;
}
QPushButton#newSession:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #b8455e, stop:1 #91294a); }
QPushButton#newSession:pressed { background:#6b1e37; }

QPushButton#ghostBtn {
  background:transparent; border:1px solid #2a2b30; border-radius:10px; padding:8px 12px; color:#9a9ca6;
}
QPushButton#ghostBtn:hover { background:#1b1c20; color:#e2e3e8; border-color:#3a3b42; }

QListWidget#sessions { background:transparent; border:none; outline:none; }
QListWidget#sessions::item { padding:10px 12px 10px 14px; border-radius:12px; margin:2px 4px; color:#b8bac2; }
QListWidget#sessions::item:hover { background:#1a1b1f; }
QListWidget#sessions::item:selected { background:#241a1e; color:#ffffff; border-left:3px solid #a6334d; }

QFrame#divider { background:#232428; max-height:1px; min-height:1px; }

QPushButton#chip {
  background:#161719; border:1px solid #2c2d33; border-radius:14px; padding:6px 14px; color:#8e9098; font-size:12px;
}
QPushButton#chip:hover { border-color:#4a3038; color:#c9cad0; }
QPushButton#chip:checked { background:#2b1a20; border-color:#a6334d; color:#f0cdd5; }

QPushButton#sendBtn {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #a6334d, stop:1 #c05570);
  border:none; border-radius:16px; min-width:44px; min-height:44px; max-width:44px; max-height:44px;
  color:#ffffff; font-weight:700; font-size:16px;
}
QPushButton#sendBtn:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #b8455e, stop:1 #d06a84); }
QPushButton#sendBtn:pressed { background:#7d2440; }
QPushButton#sendBtn:!enabled { background:#242225; color:#5f5458; }

/* busy: превращается в Стоп */
QPushButton#sendBtn[busy="true"] {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #c23754, stop:1 #8f2740);
}

QPlainTextEdit#composer {
  background:#141518; border:1px solid #2a2b30; border-radius:18px; padding:14px 18px;
  selection-background-color:#7d2440; selection-color:#ffffff;
}
QPlainTextEdit#composer:focus { border:1px solid #a6334d; }
QPlainTextEdit#composer:disabled { background:#111214; color:#575962; }

QPlainTextEdit#logView { font-family:'Cascadia Code','Consolas',monospace; font-size:12px; color:#8d8f99; }

QDockWidget { color:#6f727c; font-weight:600; }
QDockWidget::title { background:#101114; padding:9px 14px; border-top:1px solid #232428; }

QSplitter::handle { background:transparent; }
QSplitter::handle:horizontal { width:2px; }
QSplitter::handle:horizontal:hover { background:#8a3046; }

QScrollBar:vertical { background:transparent; width:10px; margin:4px 2px; }
QScrollBar::handle:vertical { background:#2a2b30; border-radius:5px; min-height:36px; }
QScrollBar::handle:vertical:hover { background:#54333c; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar:horizontal { background:transparent; height:10px; margin:2px 4px; }
QScrollBar::handle:horizontal { background:#2a2b30; border-radius:5px; min-width:36px; }
QScrollBar::handle:horizontal:hover { background:#54333c; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
QScrollArea { border:none; background:transparent; }

QLineEdit, QSpinBox, QComboBox {
  background:#161719; border:1px solid #2c2d33; border-radius:8px; padding:7px 12px; min-height:20px;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color:#c05570; }
QComboBox QAbstractItemView { background:#191a1e; border:1px solid #2c2d33; selection-background-color:#2b1a20; }
QComboBox::drop-down { border:none; width:24px; }
QCheckBox { spacing:8px; }
QCheckBox::indicator { width:16px; height:16px; border-radius:5px; border:1px solid #3a3b42; background:#161719; }
QCheckBox::indicator:checked { background:#a6334d; border-color:#a6334d; }
QToolTip { background:#1d1e22; color:#e6e7ea; border:1px solid #33343a; padding:7px 10px; border-radius:8px; }
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
        g.setColorAt(0, QColor(166, 51, 77)); g.setColorAt(1, QColor(109, 31, 49));
        p.fillPath(sp, g);
        p.end();
    }
    {   // белые углы у PNG: клипаем лого по скверклу
        QPixmap clipped(44, 44); clipped.fill(Qt::transparent);
        QPainter p(&clipped); p.setRenderHint(QPainter::Antialiasing);
        p.setClipPath(SquircleCard::squirclePath(QRectF(0.8, 0.8, 42.4, 42.4), 4.6));
        const QPixmap s = logo.scaled(44, 44, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap((44 - s.width()) / 2, (44 - s.height()) / 2, s);
        p.end();
        logo = clipped;
    }
    logoLbl_->setPixmap(logo);
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
    // было 0xC0000005: setGraphicsEffect(nullptr) сам удаляет эффект,
    // а старая лямбда потом делала fx->deleteLater() по мёртвому указателю
    auto* fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(0.0);
    auto* anim = new QPropertyAnimation(fx, "opacity", w);
    anim->setDuration(ms);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    const QPointer<QWidget> guard(w);
    QObject::connect(anim, &QAbstractAnimation::finished, w, [guard]() {
        if (guard) guard->setGraphicsEffect(nullptr); // эффект удалит сам Qt
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
        QColor(176, 84, 102), QColor(206, 116, 133),
        QColor(228, 146, 160), QColor(206, 116, 133) };
    liveCard_->setPulseColor(cols[pulsePhase_]);
}

QString MainWindow::mdToHtml(const QString& md) {
    QString s = md.toHtmlEscaped();
    s.replace(QRegularExpression(QStringLiteral("```(\\w*)\\n([\\s\\S]*?)```")),
              QStringLiteral("<pre style='background:#101114;border:1px solid #2a2b30;border-radius:10px;padding:12px;font-family:Consolas,monospace;color:#d6d9df;'>\\2</pre>"));
    s.replace(QRegularExpression(QStringLiteral("`([^`\\n]+)`")),
              QStringLiteral("<code style='background:#17181c;border:1px solid #2a2b30;border-radius:5px;padding:1px 7px;font-family:Consolas,monospace;color:#e1a9b5;'>\\1</code>"));
    s.replace(QRegularExpression(QStringLiteral("^####?\\s+(.+)$"), QRegularExpression::MultilineOption),
              QStringLiteral("<b style='color:#d98b9c;font-size:14px'>\\1</b>"));
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
    setWindowTitle(QStringLiteral("%1 — DeepSeekIDE").arg(s.title.left(56)));
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
    big->setStyleSheet(QStringLiteral("color:#f4f5f7; font-size:26px; font-weight:700;"));
    big->setAlignment(Qt::AlignCenter);
    auto* sub = new QLabel(QStringLiteral(
        "Агент читает и правит файлы проекта, выполняет команды,\n"
        "ходит в интернет, генерирует картинки и сам всё собирает."));
    sub->setStyleSheet(QStringLiteral("color:#6f727c; font-size:14px;"));
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
}

void MainWindow::addUser(const QString& text) {
    auto* c = chatAppend(QStringLiteral("user"));
    auto* l = qobject_cast<QVBoxLayout*>(c->layout());
    auto* lbl = new QLabel(mdToHtml(text));
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QStringLiteral("color:#f5f0f2;"));
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(lbl);
    pushHist(QStringLiteral("user"), text);
}

void MainWindow::beginAssistant() {
    liveCard_ = chatAppend(QStringLiteral("assistant"), QStringLiteral("live"));
    auto* l = qobject_cast<QVBoxLayout*>(liveCard_->layout());
    liveBubble_ = new QLabel(QStringLiteral("<span style='color:#575962;font-style:italic'>печатает…</span>"));
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
            ? QStringLiteral("<span style='color:#575962;font-style:italic'>…продолжаю работу —</span>")
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
    auto* lbl = new QLabel(QStringLiteral("<b style='color:%1'>%2</b> <span style='color:#6f727c'>%3</span>")
                               .arg(ok ? QStringLiteral("#35cf7f") : QStringLiteral("#ff5470"),
                                    name.toHtmlEscaped(), sub.toHtmlEscaped()));
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QStringLiteral("font-family:'Cascadia Code','Consolas',monospace;font-size:12px;"));
    l->addWidget(lbl);
    if (!detail.isEmpty()) {
        auto* det = new QLabel(QStringLiteral("<pre style='color:#7d8089;font-size:11px;font-family:Consolas,monospace;'>%1</pre>")
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

    auto* d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(QStringLiteral("Песочница — временные файлы сессии"));
    d->resize(620, 460);
    auto* vl = new QVBoxLayout(d);
    vl->setContentsMargins(16, 14, 16, 14);
    vl->setSpacing(10);

    auto* hint = new QLabel(QStringLiteral(
        "Temp-зона агента: сюда он складывает промежуточные файлы, здесь же гоняются SANDBOX SHELL "
        "и тесты. Можно закинуть свои файлы (агент их увидит) или забрать результат. Двойной клик — открыть."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#7d7f89; font-size:12px;"));
    vl->addWidget(hint);

    auto* pathLbl = new QLabel(dir);
    pathLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLbl->setStyleSheet(QStringLiteral("color:#575962; font-size:11px; font-family:Consolas,monospace;"));
    vl->addWidget(pathLbl);

    auto* list = new QListWidget; list->setObjectName(QStringLiteral("sessions"));
    vl->addWidget(list, 1);

    const std::function<void()> refresh = [list, dir]() {
        list->clear();
        const QDir qd(dir);
        const auto entries = qd.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& fi : entries) {
            const QString size = fi.isFile()
                ? QStringLiteral("  (%1 КБ)").arg(fi.size() / 1024) : QString();
            auto* it = new QListWidgetItem(fi.isDir()
                ? QStringLiteral("[папка] ") + fi.fileName() : fi.fileName() + size);
            it->setData(Qt::UserRole, fi.absoluteFilePath());
            list->addItem(it);
        }
        if (!list->count()) {
            auto* it = new QListWidgetItem(QStringLiteral("(пусто — закиньте файлы кнопкой ниже)"));
            it->setFlags(Qt::NoItemFlags);
            list->addItem(it);
        }
    };
    refresh();

    connect(list, &QListWidget::itemDoubleClicked, d, [](QListWidgetItem* it) {
        const QString path = it->data(Qt::UserRole).toString();
        if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    auto mk = [](const QString& title) {
        auto* b = new QPushButton(title);
        b->setObjectName(QStringLiteral("ghostBtn"));
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto* addBtn = mk(QStringLiteral("Закинуть файлы…"));
    auto* termBtn = mk(QStringLiteral("Терминал тут"));
    auto* openBtn = mk(QStringLiteral("Проводник"));
    auto* delBtn = mk(QStringLiteral("Удалить выбранное"));
    auto* refBtn = mk(QStringLiteral("Обновить"));
    auto* row = new QHBoxLayout; row->setSpacing(8);
    row->addWidget(addBtn); row->addWidget(termBtn); row->addWidget(openBtn);
    row->addWidget(delBtn); row->addStretch(1); row->addWidget(refBtn);
    vl->addLayout(row);

    QObject::connect(addBtn, &QPushButton::clicked, d, [d, dir, refresh]() {
        const QStringList files = QFileDialog::getOpenFileNames(d, QStringLiteral("Закинуть в песочницу"));
        const QDir qd(dir);
        for (const QString& f : files)
            QFile::copy(f, qd.filePath(QFileInfo(f).fileName()));
        if (!files.isEmpty()) refresh();
    });
    QObject::connect(termBtn, &QPushButton::clicked, d, [dir]() {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("cmd.exe"),
            QStringList{ QStringLiteral("/k"),
                         QStringLiteral("cd /d %1").arg(QDir::toNativeSeparators(dir)) });
#endif
    });
    QObject::connect(openBtn, &QPushButton::clicked, d, [dir]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    QObject::connect(delBtn, &QPushButton::clicked, d, [d, list, refresh]() {
        auto* it = list->currentItem();
        if (!it) return;
        const QString path = it->data(Qt::UserRole).toString();
        if (path.isEmpty()) return;
        const QFileInfo fi(path);
        const bool ok = fi.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!ok)
            QMessageBox::warning(d, QStringLiteral("Песочница"),
                                 QStringLiteral("Не удалось удалить: %1").arg(path));
        refresh();
    });
    QObject::connect(refBtn, &QPushButton::clicked, d, refresh);

    d->show();
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
