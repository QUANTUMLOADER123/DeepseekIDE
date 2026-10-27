#include "mainwindow.h"
#include "deepseekclient.h"
#include "agent.h"
#include "squirclecard.h"
#include "sitepreview.h"
#include "fa.h"

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
#include <QPropertyAnimation>
#include <QStyle>
#include <QEasingCurve>
#include <QAbstractAnimation>
#include <QStandardPaths>
#include <QProcess>
#include <QCloseEvent>
#include <QDateTime>
#include <QDate>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QDesktopServices>
#include <QUrl>
#include <QDialog>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QVariantAnimation>
#include <QMenu>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// ============================================================================
//  тема: графитовый фон, бордовый акцент, умеренные скругления
// ============================================================================
static const char* kTheme = R"(
QMainWindow, QDialog { background:#0c0d0f; }
QWidget { color:#e6e7ea; font-size:13px; font-family:'Segoe UI','Inter',sans-serif; background:transparent; }
*[themeRoot="true"] { background:#0c0d0f; }

QWidget#titleBar { background:#101114; border-bottom:1px solid #232428; }
QLabel#tbName { color:#d8dade; font-size:12px; font-weight:700; }
QPushButton#winBtn { background:transparent; border:none; min-width:42px; max-width:42px; min-height:34px; max-height:34px; }
QPushButton#winBtn:hover { background:#1e1f24; }
QPushButton#winBtn:pressed { background:#26272c; }
QPushButton#winClose { background:transparent; border:none; min-width:42px; max-width:42px; min-height:34px; max-height:34px; }
QPushButton#winClose:hover { background:#a6334d; }
QPushButton#winClose:pressed { background:#7d2440; }

QFrame#sidePanel { background:#131417; border:none; border-right:1px solid #232428; }

QPushButton#newSession {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #a6334d, stop:1 #7d2440);
  border:none; border-radius:8px; padding:11px 12px; color:#ffffff; font-weight:600; font-size:13px;
}
QPushButton#newSession:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #b8455e, stop:1 #91294a); }
QPushButton#newSession:pressed { background:#6b1e37; }

QPushButton#ghostBtn {
  background:transparent; border:1px solid #2a2b30; border-radius:8px; padding:8px 12px; color:#9a9ca6;
}
QPushButton#ghostBtn:hover { background:#1b1c20; color:#e2e3e8; border-color:#3a3b42; }

QListWidget#sessions { background:transparent; border:none; outline:none; }
QListWidget#sessions::item { padding:6px 10px; border-radius:8px; margin:1px 6px; color:#b8bac2; font-size:12px; }

QFrame#codeBox { background:#101114; border:1px solid #2a2b30; border-radius:8px; }
QPlainTextEdit#codeArea {
  background:transparent; border:none; padding:2px 10px 8px 10px;
  font-family:'Cascadia Code','Consolas',monospace; font-size:12px; color:#d6d9df;
  selection-background-color:#7d2440;
}
QPushButton#miniBtn { background:transparent; border:none; border-radius:5px; min-width:20px; max-width:20px; min-height:20px; max-height:20px; padding:0; }
QPushButton#miniBtn:hover { background:#1e1f24; }
QPushButton#miniBtn:pressed { background:#26272c; }
QPushButton#actBtn { background:transparent; border:none; border-radius:6px; padding:3px 8px; color:#6f727c; font-size:11px; }
QPushButton#actBtn:hover { background:#1e1f24; color:#e2e3e8; }
QPushButton#actBtn:pressed { background:#26272c; }
QListWidget#sessions::item:hover { color:#e2e3e8; }  /* фон даёт анимированная пилюля */
QListWidget#sessions::item:selected { background:#0f1013; color:#ffffff; border-left:2px solid #a6334d; }

QPushButton#chipIcn {
  background:transparent; border:1px solid transparent; border-radius:7px;
  padding:4px 10px 4px 8px; color:#7d7f89; font-size:12px;
}
QPushButton#chipIcn:hover { background:#1e1f24; color:#c9cad0; }
QPushButton#chipIcn:checked { background:#2b1a20; border-color:#5a2634; color:#f0cdd5; }

QFrame#composerBox { background:#141518; border:1px solid #2a2b30; border-radius:12px; }
QFrame#composerBox[focus="true"] { border:1px solid #a6334d; }

QPlainTextEdit#composer {
  background:transparent; border:none; padding:10px 12px 2px 12px; font-size:13px;
  selection-background-color:#7d2440; selection-color:#ffffff;
}
QPlainTextEdit#composer:disabled { color:#575962; }

QPushButton#sendBtn {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #a6334d, stop:1 #c05570);
  border:none; border-radius:8px; min-width:30px; min-height:30px; max-width:30px; max-height:30px;
}
QPushButton#sendBtn:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #b8455e, stop:1 #d06a84); }
QPushButton#sendBtn:pressed { background:#7d2440; }
QPushButton#sendBtn:!enabled { background:#242225; }
QPushButton#sendBtn[busy="true"] {
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #c23754, stop:1 #8f2740);
}

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
  background:#161719; border:1px solid #2c2d33; border-radius:7px; padding:7px 12px; min-height:20px;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color:#c05570; }
QComboBox QAbstractItemView { background:#191a1e; border:1px solid #2c2d33; selection-background-color:#2b1a20; }
QComboBox::drop-down { border:none; width:24px; }
QCheckBox { spacing:8px; }
QCheckBox::indicator { width:16px; height:16px; border-radius:4px; border:1px solid #3a3b42; background:#161719; }
QCheckBox::indicator:checked { background:#a6334d; border-color:#a6334d; }
QToolTip { background:#1d1e22; color:#e6e7ea; border:1px solid #33343a; padding:7px 10px; border-radius:6px; }

QMenu { background:#191a1e; border:1px solid #2c2d33; border-radius:8px; padding:4px; }
QMenu::item { padding:6px 20px 6px 14px; border-radius:5px; color:#c9cad0; }
QMenu::item:selected { background:#2b1a20; color:#f0cdd5; }
QMenu::separator { height:1px; background:#2a2b30; margin:4px 8px; }

QWidget#toolLine { background:#15161a; border:1px solid #25262c; border-radius:10px; }
)";

// ============================================================================
//  лого: клип по скруглённому квадрату (без белых углов PNG)
// ============================================================================
static QPixmap logoPix(int size) {
    QString logoPath = QStringLiteral("assets/logo.png");
    if (!QFile::exists(logoPath))
        logoPath = QCoreApplication::applicationDirPath() + QStringLiteral("/assets/logo.png");
    QPixmap src(logoPath);
    if (src.isNull()) { // фолбэк: бордовый градиентный квадрат
        src = QPixmap(size, size); src.fill(Qt::transparent);
        QPainter p(&src); p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, size, size);
        g.setColorAt(0, QColor(166, 51, 77)); g.setColorAt(1, QColor(109, 31, 49));
        p.fillPath(SquircleCard::roundPath(QRectF(1, 1, size - 2, size - 2), size * 0.22), g);
        p.end();
    }
    QPixmap out(size, size); out.fill(Qt::transparent);
    QPainter p(&out); p.setRenderHint(QPainter::Antialiasing);
    p.setClipPath(SquircleCard::roundPath(QRectF(0.5, 0.5, size - 1.0, size - 1.0), size * 0.22));
    const QPixmap s = src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    p.drawPixmap((size - s.width()) / 2, (size - s.height()) / 2, s);
    p.end();
    return out;
}

// круговой спиннер «печатает» — крутится дугой
class Spinner : public QWidget {
public:
    explicit Spinner(QWidget* p = nullptr) : QWidget(p) { setFixedSize(16, 16); }
    void advance() { angle_ = (angle_ + 24) % 360; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r(2, 2, 12, 12);
        QPen track(QColor(0x2a, 0x2b, 0x30)); track.setWidthF(2.0);
        p.setPen(track); p.drawEllipse(r);
        QPen pen(QColor(0xc0, 0x55, 0x70)); pen.setWidthF(2.2); pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen); p.drawArc(r, angle_ * 16, 100 * 16);
    }
    int angle_ = 0;
};

// живой стрим БЕЗ тулок: закрытые маркеры, ops-блоки и хвост недописанного маркера
static QString liveClean(const QString& raw) {
    QString s = raw;
    static const QRegularExpression fullRe(QStringLiteral("\\[\\s*<\\{[\\s\\S]*?(?:\\}\\s*>\\s*\\]|\\}\\s*\\])"));
    s.remove(fullRe);
    static const QRegularExpression opsRe(QStringLiteral("```deepseekide-ops[\\s\\S]*?(?:```|$)"));
    s.remove(opsRe);
    int cut = s.lastIndexOf(QStringLiteral("[<{"));
    if (cut >= 0) s = s.left(cut);
    cut = s.lastIndexOf(QStringLiteral("&lt;{"));
    if (cut >= 0) s = s.left(cut);
    return s;
}

static QPixmap roundedPm(const QPixmap& src, qreal rad) {
    QPixmap out(src.size()); out.fill(Qt::transparent);
    QPainter p(&out); p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path; path.addRoundedRect(QRectF(0, 0, src.width(), src.height()), rad, rad);
    p.setClipPath(path); p.drawPixmap(0, 0, src);
    return out;
}

// слайд-ап при появлении карточки (ступенька вверх)
static void slideUp(QWidget* w, QHBoxLayout* lay) {
    lay->setContentsMargins(26, 10, 26, 0);
    auto* a = new QVariantAnimation(w);
    a->setDuration(240);
    a->setStartValue(10);
    a->setEndValue(0);
    a->setEasingCurve(QEasingCurve::OutExpo);
    QObject::connect(a, &QVariantAnimation::valueChanged, w, [lay](const QVariant& v) {
        lay->setContentsMargins(26, v.toInt(), 26, 0);
    });
    QObject::connect(a, &QAbstractAnimation::finished, a, &QObject::deleteLater);
    a->start();
}

// ============================================================================
MainWindow::MainWindow() {
    cfg_ = Cfg::load();
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);   // своя шапка вместо системной
    setWindowTitle(QStringLiteral("DeepSeekIDE"));
    resize(1280, 840);
    setMinimumSize(980, 620);
    qApp->setStyleSheet(QLatin1String(kTheme));

    ds_ = new DeepSeekClient(this);
    ds_->setCfg(cfg_);
    agent_ = new Agent(ds_, this);
    agent_->setCfg(cfg_);

    // ==================== кастомный титлбар ====================
    titleBar_ = new QWidget;
    titleBar_->setObjectName(QStringLiteral("titleBar"));
    titleBar_->setFixedHeight(34);
    titleBar_->installEventFilter(this);
    auto* tl = new QHBoxLayout(titleBar_);
    tl->setContentsMargins(12, 0, 0, 0);
    tl->setSpacing(8);

    logoLbl_ = new QLabel;
    logoLbl_->setPixmap(logoPix(18));
    tl->addWidget(logoLbl_);
    auto* tbName = new QLabel(QStringLiteral("DeepSeekIDE"));
    tbName->setObjectName(QStringLiteral("tbName"));
    tl->addWidget(tbName);
    tl->addStretch(1);

    auto mkWin = [this](const QString& glyph, bool close) {
        auto* b = new QPushButton(titleBar_);
        b->setObjectName(close ? QStringLiteral("winClose") : QStringLiteral("winBtn"));
        b->setIcon(QIcon(fa::pix(glyph, 11, QColor(0x9a, 0x9c, 0xa6))));
        b->setIconSize(QSize(11, 11));
        b->setCursor(Qt::ArrowCursor);
        return b;
    };
    auto* winMin = mkWin(fa::winMin(), false);
    winMaxBtn_   = mkWin(fa::winMax(), false);
    auto* winCls = mkWin(fa::xmark(), true);
    connect(winMin, &QPushButton::clicked, this, [this] { showMinimized(); });
    connect(winMaxBtn_, &QPushButton::clicked, this, &MainWindow::toggleMax);
    connect(winCls, &QPushButton::clicked, this, &QWidget::close);
    tl->addWidget(winMin); tl->addWidget(winMaxBtn_); tl->addWidget(winCls);

    // ==================== левая панель: сессии ====================
    auto* side = new QFrame; side->setObjectName(QStringLiteral("sidePanel"));
    side->setMinimumWidth(264); side->setMaximumWidth(264);
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(14, 12, 14, 12);
    sl->setSpacing(10);

    auto* newBtn = new QPushButton(QStringLiteral("New Chat"));
    newBtn->setObjectName(QStringLiteral("newSession"));
    newBtn->setCursor(Qt::PointingHandCursor);
    fa::apply(newBtn, fa::plus(), QColor(0xff, 0xff, 0xff), 12);
    connect(newBtn, &QPushButton::clicked, this, &MainWindow::onNewSession);
    sl->addWidget(newBtn);

    sessionList_ = new QListWidget; sessionList_->setObjectName(QStringLiteral("sessions"));
    sessionList_->setSpacing(1);
    sessionList_->setMouseTracking(true);
    connect(sessionList_, &QListWidget::currentRowChanged, this, &MainWindow::onPickSession);
    // ползущая пилюля-наведение поверх строк (полупрозрачная — текст остаётся читаемым)
    hoverPill_ = new QWidget(sessionList_->viewport());
    hoverPill_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hoverPill_->setStyleSheet(QStringLiteral("background:rgba(255,255,255,9); border-radius:8px;"));
    hoverPill_->hide();
    sessionList_->viewport()->installEventFilter(this);
    pillAnim_ = new QPropertyAnimation(hoverPill_, "geometry", this);
    pillAnim_->setDuration(150);
    pillAnim_->setEasingCurve(QEasingCurve::OutCubic);
    sl->addWidget(sessionList_, 1);

    auto* bottom = new QVBoxLayout; bottom->setSpacing(8);
    auto* sbxBtn = new QPushButton(QStringLiteral("Sandbox"));
    sbxBtn->setObjectName(QStringLiteral("ghostBtn"));
    sbxBtn->setCursor(Qt::PointingHandCursor);
    fa::apply(sbxBtn, fa::box(), QColor(0x9a, 0x9c, 0xa6));
    connect(sbxBtn, &QPushButton::clicked, this, &MainWindow::onOpenSandbox);
    auto* setBtn = new QPushButton(QStringLiteral("Settings"));
    setBtn->setObjectName(QStringLiteral("ghostBtn"));
    setBtn->setCursor(Qt::PointingHandCursor);
    fa::apply(setBtn, fa::gear(), QColor(0x9a, 0x9c, 0xa6));
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

    chatScroll_ = new QScrollArea;
    chatScroll_->setWidgetResizable(true);
    auto* chatHost = new QWidget;
    chatLay_ = new QVBoxLayout(chatHost);
    chatLay_->setContentsMargins(0, 20, 0, 20);
    chatLay_->setSpacing(12);
    chatLay_->addStretch(1);
    chatScroll_->setWidget(chatHost);
    cl->addWidget(chatScroll_, 1);

    // композер: рамка-бокс, сверху текст, снизу чипы + отправка
    auto* bottomWrap = new QWidget;
    auto* bw = new QVBoxLayout(bottomWrap);
    bw->setContentsMargins(26, 4, 26, 18);
    bw->setSpacing(0);

    composerBox_ = new QFrame;
    composerBox_->setObjectName(QStringLiteral("composerBox"));
    auto* cv = new QVBoxLayout(composerBox_);
    cv->setContentsMargins(4, 4, 8, 6);
    cv->setSpacing(2);

    input_ = new QPlainTextEdit; input_->setObjectName(QStringLiteral("composer"));
    input_->setPlaceholderText(QStringLiteral("Describe the task or ask a question…"));
    input_->setMinimumHeight(34);
    input_->setMaximumHeight(96);
    input_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    input_->installEventFilter(this);
    new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Return)), this, SLOT(onSend()));
    new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Enter)), this, SLOT(onSend()));
    cv->addWidget(input_);

    // строка прикреплённых файлов (прячется, когда пусто)
    attachRow_ = new QWidget;
    auto* ar = new QHBoxLayout(attachRow_);
    ar->setContentsMargins(8, 6, 10, 0);
    ar->setSpacing(6);
    auto* clipIco = new QLabel;
    clipIco->setPixmap(fa::pix(fa::paperclip(), 10, QColor(0x7d, 0x7f, 0x89)));
    ar->addWidget(clipIco);
    attachLbl_ = new QLabel;
    attachLbl_->setStyleSheet(QStringLiteral("color:#7d7f89; font-size:11px;"));
    ar->addWidget(attachLbl_, 1);
    auto* clr = new QPushButton;
    clr->setObjectName(QStringLiteral("miniBtn"));
    clr->setCursor(Qt::PointingHandCursor);
    clr->setIcon(QIcon(fa::pix(fa::xmark(), 10, QColor(0x7d, 0x7f, 0x89))));
    clr->setIconSize(QSize(10, 10));
    connect(clr, &QPushButton::clicked, this, [this] { attachPaths_.clear(); refreshAttachUi(); });
    ar->addWidget(clr);
    attachRow_->hide();
    cv->insertWidget(0, attachRow_);

    auto* crow = new QWidget;
    auto* cr = new QHBoxLayout(crow);
    cr->setContentsMargins(6, 0, 0, 0);
    cr->setSpacing(2);

    auto mkChip = [](const QString& title, const QString& g) {
        auto* b = new QPushButton(title);
        b->setObjectName(QStringLiteral("chipIcn"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("faGlyph", g);
        return b;
    };
    auto paintChip = [](QPushButton* b) {
        const QColor c = b->isChecked() ? QColor(0xf0, 0xcd, 0xd5) : QColor(0x7d, 0x7f, 0x89);
        b->setIcon(QIcon(fa::pix(b->property("faGlyph").toString(), 11, c)));
        b->setIconSize(QSize(11, 11));
    };
    chipThink_  = mkChip(QStringLiteral("Deep Think"), fa::brain());
    chipSearch_ = mkChip(QStringLiteral("Search"),     fa::search());
    chipImg_    = mkChip(QStringLiteral("Images"),     fa::image());
    chipThink_->setChecked(cfg_.thinking);
    chipSearch_->setChecked(cfg_.search);
    chipImg_->setChecked(cfg_.imgOn);
    for (auto* c : {chipThink_, chipSearch_, chipImg_}) {
        paintChip(c);
        connect(c, &QPushButton::toggled, this, [c, paintChip](bool) { paintChip(c); });
    }
    connect(chipThink_, &QPushButton::toggled, this, [this](bool on){ cfg_.thinking = on; Cfg::save(cfg_); ds_->setCfg(cfg_); });
    connect(chipSearch_, &QPushButton::toggled, this, [this](bool on){ cfg_.search = on; Cfg::save(cfg_); ds_->setCfg(cfg_); });
    connect(chipImg_, &QPushButton::toggled, this, [this](bool on){ cfg_.imgOn = on; Cfg::save(cfg_); agent_->setCfg(cfg_); });
    cr->addWidget(chipThink_);
    cr->addWidget(chipSearch_);
    cr->addWidget(chipImg_);

    cr->addSpacing(6);
    auto* attachBtn = new QPushButton(QStringLiteral("Attach"));
    attachBtn->setObjectName(QStringLiteral("chipIcn"));
    attachBtn->setCursor(Qt::PointingHandCursor);
    attachBtn->setIcon(QIcon(fa::pix(fa::paperclip(), 11, QColor(0x7d, 0x7f, 0x89))));
    attachBtn->setIconSize(QSize(11, 11));
    attachBtn->setToolTip(QStringLiteral("Attach files (agent gets the paths and can read them)"));
    connect(attachBtn, &QPushButton::clicked, this, &MainWindow::onAttach);
    cr->addWidget(attachBtn);
    folderPill_ = new QPushButton(QStringLiteral("no folder"));
    folderPill_->setObjectName(QStringLiteral("chipIcn"));
    folderPill_->setCursor(Qt::PointingHandCursor);
    folderPill_->setIcon(QIcon(fa::pix(fa::folder(), 11, QColor(0x7d, 0x7f, 0x89))));
    folderPill_->setIconSize(QSize(11, 11));
    connect(folderPill_, &QPushButton::clicked, this, &MainWindow::onPickFolder);
    cr->addWidget(folderPill_);

    cr->addStretch(1);

    sendBtn_ = new QPushButton;
    sendBtn_->setObjectName(QStringLiteral("sendBtn"));
    sendBtn_->setCursor(Qt::PointingHandCursor);
    sendBtn_->setToolTip(QStringLiteral("Send (Ctrl+Enter)"));
    sendBtn_->setIcon(QIcon(fa::pix(fa::arrowUp(), 12, QColor(0xff, 0xff, 0xff))));
    sendBtn_->setIconSize(QSize(12, 12));
    connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::onSend);
    cr->addWidget(sendBtn_, 0, Qt::AlignBottom);

    cv->addWidget(crow);
    bw->addWidget(composerBox_);
    cl->addWidget(bottomWrap);

    // ==================== сплит + корень ====================
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
    root->setSpacing(0);
    root->addWidget(titleBar_);
    root->addWidget(split, 1);
    setCentralWidget(central);

    // лог-док (по умолчанию скрыт, F10)
    logView_ = new QPlainTextEdit; logView_->setObjectName(QStringLiteral("logView"));
    logView_->setReadOnly(true); logView_->setMaximumBlockCount(2000);
    auto* logDock = new QDockWidget(QStringLiteral("LOG"), this);
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
    connect(agent_, &Agent::compactDone, this, &MainWindow::onCompactDone);
    connect(agent_, &Agent::titleReady, this, [this](const QString& tt) {
        if (tt.isEmpty() || curSess_ < 0) return;
        Session& s = sessions_[curSess_];
        if (s.title != QStringLiteral("New Chat")) return;
        s.title = tt;
        setWindowTitle(QStringLiteral("%1 — DeepSeekIDE").arg(s.title.left(56)));
        refreshSessionList();
        persistNow();
    });
    connect(agent_, &Agent::log, this, &MainWindow::logLine);
    connect(ds_, &DeepSeekClient::log, this, &MainWindow::logLine);

    connect(&pulseTimer_, &QTimer::timeout, this, &MainWindow::pulseTick);
    pulseTimer_.setInterval(150);

    // ==================== сессии ====================
    sessions_ = SessionStore().load();
    refreshSessionList();
    if (sessions_.isEmpty()) {
        Session s = Session::create(cfg_.projectDir);
        sessions_.prepend(s);
        persistNow();
        refreshSessionList();
    }
    activateSession(0);

    logLine(cfg_.token.isEmpty()
        ? QStringLiteral("No token set. Settings (bottom-left) -> DeepSeek token.")
        : QStringLiteral("Ready. Describe the task."));
}

// ============================================================================
//  frameless: drag титлбаром + ресайз за края (WM_NCHITTEST)
// ============================================================================
bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == titleBar_) {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) {
                dragOn_ = true;
                dragOffset_ = me->globalPosition().toPoint() - frameGeometry().topLeft();
                return false;
            }
        } else if (ev->type() == QEvent::MouseMove && dragOn_) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (isMaximized()) {
                // вытаскиваем из maximized за курсор
                const QPoint gp = me->globalPosition().toPoint();
                showNormal();
                move(gp.x() - width() / 2, gp.y() - 16);
                dragOffset_ = QPoint(width() / 2, 16);
            } else {
                move(me->globalPosition().toPoint() - dragOffset_);
            }
            return false;
        } else if (ev->type() == QEvent::MouseButtonRelease) {
            dragOn_ = false;
        } else if (ev->type() == QEvent::MouseButtonDblClick) {
            toggleMax();
            return true;
        }
    } else if (obj == sessionList_->viewport()) {
        if (ev->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(ev);
            const QModelIndex ix = sessionList_->indexAt(me->pos());
            if (ix.isValid() && sessionList_->item(ix.row())
                && sessionList_->item(ix.row())->data(Qt::UserRole).toInt() >= 0) {
                const QRect r = sessionList_->visualRect(ix);
                pillAnim_->stop();
                pillAnim_->setStartValue(hoverPill_->isVisible()
                    ? hoverPill_->geometry()
                    : QRect(r.x() + 4, r.y() + 1, r.width() - 8, r.height() - 2));
                pillAnim_->setEndValue(QRect(r.x() + 4, r.y() + 1, r.width() - 8, r.height() - 2));
                hoverPill_->show();
                pillAnim_->start();
            } else {
                hoverPill_->hide();
            }
        } else if (ev->type() == QEvent::Leave) {
            hoverPill_->hide();
        }
    } else if (obj == input_) {
        if (ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                && !(ke->modifiers() & Qt::ShiftModifier)) {
                if (!input_->isEnabled()) return true;
                onSend();
                return true;
            }
        }
        if (ev->type() == QEvent::FocusIn) {
            composerBox_->setProperty("focus", true);
            composerBox_->style()->unpolish(composerBox_);
            composerBox_->style()->polish(composerBox_);
        } else if (ev->type() == QEvent::FocusOut) {
            composerBox_->setProperty("focus", false);
            composerBox_->style()->unpolish(composerBox_);
            composerBox_->style()->polish(composerBox_);
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::toggleMax() {
    if (isMaximized()) showNormal(); else showMaximized();
    winMaxBtn_->setIcon(QIcon(fa::pix(isMaximized() ? fa::winRestore() : fa::winMax(), 11,
                                      QColor(0x9a, 0x9c, 0xa6))));
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST && !isMaximized()) {
            const int b = 6;
            POINT pt{ GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
            ScreenToClient(msg->hwnd, &pt);
            const int w = width(), h = height();
            const bool l = pt.x < b, r = pt.x > w - b, t = pt.y < b, bt = pt.y > h - b;
            if (t && l)      { *result = HTTOPLEFT;     return true; }
            if (t && r)      { *result = HTTOPRIGHT;    return true; }
            if (bt && l)     { *result = HTBOTTOMLEFT;  return true; }
            if (bt && r)     { *result = HTBOTTOMRIGHT; return true; }
            if (l)  { *result = HTLEFT;   return true; }
            if (r)  { *result = HTRIGHT;  return true; }
            if (t)  { *result = HTTOP;    return true; }
            if (bt) { *result = HTBOTTOM; return true; }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

// ============================================================================
//  карточки чата — динамическая ширина по содержимому
// ============================================================================
SquircleCard* MainWindow::chatAppend(const QString& role, const QString& state, bool animate) {
    // сносим empty-state, когда появляется первое реальное сообщение
    for (int i = chatLay_->count() - 2; i >= 0; --i) {
        QLayoutItem* it = chatLay_->itemAt(i);
        QWidget* w = it ? it->widget() : nullptr;
        if (w && w->property("emptyState").toBool()) { chatLay_->removeItem(it); delete w; }
    }
    auto* card = new SquircleCard(role);
    if (!state.isEmpty()) card->setState(state);
    auto* l = new QVBoxLayout(card);
    l->setContentsMargins(16, 11, 16, 11);
    l->setSpacing(6);

    auto* wrap = new QWidget;
    auto* hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(26, 0, 26, 0);
    if (role == QStringLiteral("user")) {
        hl->addStretch(1);
        hl->addWidget(card, 0);
    } else {
        hl->addWidget(card, 0);
        hl->addStretch(1);
    }
    chatLay_->insertWidget(chatLay_->count() - 1, wrap);
    if (animate) { fadeIn(wrap); slideUp(wrap, hl); }
    QTimer::singleShot(0, this, [this] { smoothScrollBottom(); });
    return card;
}

void MainWindow::fitBubble(SquircleCard* card, const QString& text, bool userSide) {
    const int vw = chatScroll_->viewport() && chatScroll_->viewport()->width() > 0
                       ? chatScroll_->viewport()->width() : 900;
    const int limit = qMax(240, qMin(vw - 240, userSide ? 520 : 660));
    QString plain = text;
    if (text.contains(QLatin1Char('<'))) {
        QTextDocument td; td.setHtml(text); plain = td.toPlainText();
    }
    plain.remove(QRegularExpression(QStringLiteral("```[\\s\\S]*?```"))); // код не раздувает ширину
    const QFontMetrics fm(QFont(QStringLiteral("Segoe UI"), 9));
    int maxw = 0;
    for (const QString& ln : plain.split(QLatin1Char('\n')))
        maxw = qMax(maxw, fm.horizontalAdvance(ln.left(400)));
    const int w = qBound(64, maxw + 36, limit);
    card->setFixedWidth(w);
}

// то же измерение, но карточку ТОЛЬКО расширяем (тулы/картинки в общем бабле)
void MainWindow::fitGrow(SquircleCard* card, const QString& text) {
    const int vw = chatScroll_->viewport() && chatScroll_->viewport()->width() > 0
                       ? chatScroll_->viewport()->width() : 900;
    const int limit = qMax(240, qMin(vw - 240, 660));
    const QFontMetrics fm(QFont(QStringLiteral("Cascadia Code"), 9));
    int maxw = 0;
    for (const QString& ln : text.split(QLatin1Char('\n')))
        maxw = qMax(maxw, fm.horizontalAdvance(ln.left(400)));
    const int w = qBound(64, maxw + 44, limit);
    if (card->width() < w) card->setFixedWidth(w);
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
    if (liveCard_) {
        pulsePhase_ = (pulsePhase_ + 1) % 4;
        static const QColor cols[4] = {
            QColor(176, 84, 102), QColor(206, 116, 133),
            QColor(228, 146, 160), QColor(206, 116, 133) };
        liveCard_->setPulseColor(cols[pulsePhase_]);
    }
    if (spinner_) static_cast<Spinner*>(spinner_)->advance();
}

QString MainWindow::mdToHtml(const QString& md) {
    QString s = md.toHtmlEscaped();
    s.replace(QRegularExpression(QStringLiteral("```(\\w*)\\n([\\s\\S]*?)```")),
              QStringLiteral("<pre style='background:#101114;border:1px solid #2a2b30;border-radius:8px;padding:10px;font-family:Consolas,monospace;color:#d6d9df;'>\\2</pre>"));
    s.replace(QRegularExpression(QStringLiteral("`([^`\\n]+)`")),
              QStringLiteral("<code style='background:#17181c;border:1px solid #2a2b30;border-radius:4px;padding:1px 6px;font-family:Consolas,monospace;color:#e1a9b5;'>\\1</code>"));
    s.replace(QRegularExpression(QStringLiteral("^#\\s+(.+)$"), QRegularExpression::MultilineOption),
              QStringLiteral("<span style='color:#d98b9c;font-size:17px;font-weight:700'>\\1</span><br>"));
    s.replace(QRegularExpression(QStringLiteral("^##\\s+(.+)$"), QRegularExpression::MultilineOption),
              QStringLiteral("<span style='color:#d98b9c;font-size:15px;font-weight:700'>\\1</span><br>"));
    s.replace(QRegularExpression(QStringLiteral("^####?\\s+(.+)$"), QRegularExpression::MultilineOption),
              QStringLiteral("<b style='color:#d98b9c;font-size:14px'>\\1</b>"));
    s.replace(QRegularExpression(QStringLiteral("\\*\\*([^*\\n]+)\\*\\*")),
              QStringLiteral("<b>\\1</b>"));
    s.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return s;
}

// ============================================================================
//  сессии (группы TODAY / YESTERDAY / OLDER по полю updated)
// ============================================================================
void MainWindow::persistNow() {
    if (curSess_ >= 0 && curSess_ < sessions_.size()) {
        sessions_[curSess_].dsSessionId = ds_->sessionId();
        sessions_[curSess_].parentMessageId = ds_->parentMessageId();
    }
    SessionStore().save(sessions_);
}

int MainWindow::sessionRowOf(int idx) const {
    if (idx < 0) return -1;
    for (int r = 0; r < sessionList_->count(); ++r)
        if (sessionList_->item(r)->data(Qt::UserRole).toInt() == idx) return r;
    return -1;
}

void MainWindow::refreshSessionList() {
    sessionList_->blockSignals(true);
    sessionList_->clear();
    const QDate today = QDate::currentDate();
    static const char* GNAME[3] = { "Today", "Yesterday", "Older" };
    bool hdr[3] = { false, false, false };
    QFont hf; hf.setBold(true); hf.setPointSize(qMax(7, hf.pointSize() - 3));
    for (int i = 0; i < sessions_.size(); ++i) {
        const Session& s = sessions_[i];
        QDate d = QDateTime::fromString(s.updated.isEmpty() ? s.created : s.updated, Qt::ISODate).date();
        if (!d.isValid()) d = today;
        const qint64 days = d.daysTo(today);
        const int g = days <= 0 ? 0 : (days == 1 ? 1 : 2);
        if (!hdr[g]) {
            hdr[g] = true;
            auto* h = new QListWidgetItem(QString::fromLatin1(GNAME[g]));
            h->setFlags(Qt::NoItemFlags);
            h->setData(Qt::UserRole, -1);
            h->setForeground(QColor(0x57, 0x59, 0x62));
            h->setFont(hf);
            h->setSizeHint(QSize(0, 20));
            sessionList_->addItem(h);
        }
        auto* it = new QListWidgetItem(
            QIcon(fa::pix(fa::comment(), 11, QColor(0x6f, 0x72, 0x7c))),
            s.title.left(38) + (s.title.size() > 38 ? QStringLiteral("…") : QString()));
        it->setData(Qt::UserRole, i);
        it->setToolTip(QStringLiteral("%1\n%2").arg(s.title, s.folder));
        it->setSizeHint(QSize(0, 28));
        sessionList_->addItem(it);
    }
    sessionList_->blockSignals(false);
    const int row = sessionRowOf(curSess_);
    if (row >= 0) sessionList_->setCurrentRow(row);
}

void MainWindow::activateSession(int i) {
    if (i < 0 || i >= sessions_.size()) return;
    curSess_ = i;
    const int row = sessionRowOf(i);
    if (row >= 0) sessionList_->setCurrentRow(row);
    const Session& s = sessions_[i];
    setWindowTitle(QStringLiteral("%1 — DeepSeekIDE").arg(s.title.left(56)));
    folderPill_->setText(QStringLiteral(" ") + (s.folder.isEmpty()
        ? QStringLiteral("no folder") : QFileInfo(s.folder).fileName()));
    folderPill_->setToolTip(s.folder);

    clearChatUi();
    replaying_ = true;
    replayHistory();
    replaying_ = false;
    if (s.history.isEmpty()) showEmptyState();
    if (chatScroll_->widget()) fadeIn(chatScroll_->widget(), 180);

    ds_->setSession(s.dsSessionId, s.parentMessageId);
    agent_->setProjectRoot(s.folder);
    agent_->restoreSession(!s.dsSessionId.isEmpty());
}

void MainWindow::onPickSession(int row) {
    if (row < 0 || row >= sessionList_->count()) return;
    const int idx = sessionList_->item(row)->data(Qt::UserRole).toInt();
    if (idx < 0 || busyOn_ || idx == curSess_ || idx >= sessions_.size()) return;
    persistNow();
    activateSession(idx);
}

void MainWindow::onNewSession() {
    if (busyOn_) return;
    persistNow();          // старую сессию — пока curSess_ ещё старый
    Session s = Session::create(curSess_ >= 0 ? sessions_[curSess_].folder : cfg_.projectDir);
    sessions_.prepend(s);
    curSess_ = 0;
    refreshSessionList();
    activateSession(0);    // ds_->setSession("", "") — новая память у DeepSeek
    // persist ПОСЛЕ activate: persistNow копирует ds_->sessionId() (уже пустой),
    // иначе старый chat id записался бы в новую сессию («помнит прошлые»)
    persistNow();
}

void MainWindow::setSessionTitleFromText(const QString& t) {
    if (curSess_ < 0) return;
    Session& s = sessions_[curSess_];
    if (!s.title.isEmpty() && s.title != QStringLiteral("New Chat")) return;
    s.title = t.simplified().left(48);
    if (s.title.isEmpty()) s.title = QStringLiteral("Chat");
    setWindowTitle(QStringLiteral("%1 — DeepSeekIDE").arg(s.title.left(56)));
    refreshSessionList();
    persistNow();
}

void MainWindow::clearChatUi() {
    pulseTimer_.stop();
    liveCard_ = nullptr; liveBubble_ = nullptr; spinner_ = nullptr; liveRaw_.clear();
    groupText_.clear(); replayCard_ = nullptr;
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
    wrap->setProperty("emptyState", true);
    auto* vl = new QVBoxLayout(wrap);
    vl->setAlignment(Qt::AlignCenter);
    auto* icon = new QLabel;
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(fa::pix(fa::sparkles(), 30, QColor(0xa6, 0x33, 0x4d)));
    auto* big = new QLabel(QStringLiteral("What are we building?"));
    big->setStyleSheet(QStringLiteral("color:#f4f5f7; font-size:24px; font-weight:700;"));
    big->setAlignment(Qt::AlignCenter);
    auto* sub = new QLabel(QStringLiteral(
        "The agent reads and edits your project files, runs commands,\n"
        "browses the web and generates images — all inside the chosen folder."));
    sub->setStyleSheet(QStringLiteral("color:#6f727c; font-size:13px;"));
    sub->setAlignment(Qt::AlignCenter);
    vl->addWidget(icon);
    vl->addSpacing(6);
    vl->addWidget(big);
    vl->addSpacing(6);
    vl->addWidget(sub);
    chatLay_->insertWidget(chatLay_->count() - 1, wrap);
}

int MainWindow::pushHist(const QString& role, const QString& text, const QString& name,
                         const QString& sub, bool ok, const QString& detail) {
    if (replaying_ || curSess_ < 0 || curSess_ >= sessions_.size()) return -1;
    QJsonObject e{{QStringLiteral("role"), role}};
    if (!text.isEmpty()) e[QStringLiteral("text")] = text;
    if (!name.isEmpty()) e[QStringLiteral("name")] = name;
    if (!sub.isEmpty()) e[QStringLiteral("sub")] = sub;
    e[QStringLiteral("ok")] = ok;
    if (!detail.isEmpty()) e[QStringLiteral("detail")] = detail;
    if (role == QLatin1String("user"))
        e[QStringLiteral("pprev")] = ds_->parentMessageId(); // точка отката для Regenerate
    sessions_[curSess_].history.append(e);
    if (sessions_[curSess_].history.size() > 400) sessions_[curSess_].history.removeFirst();
    Session& s = sessions_[curSess_];
    s.ctxChars += text.size() + name.size() + sub.size() + detail.size();
    persistNow();
    return s.history.size() - 1;
}

void MainWindow::replayHistory() {
    if (curSess_ < 0 || curSess_ >= sessions_.size()) return;
    const auto& hist = sessions_[curSess_].history;
    // группировка: всё от агента (текст + тулки) между двумя сообщениями юзера — в ОДНОЙ карточке
    SquircleCard* grp = nullptr;
    QString grpText;
    int grpIdx = -1;
    auto grpLayout = [&]() { return qobject_cast<QVBoxLayout*>(grp->layout()); };
    auto closeGrp = [&]() {
        if (grp) {
            fitBubble(grp, grpText.isEmpty() ? QStringLiteral("…") : grpText, false);
            if (!grpText.isEmpty()) addAssistantActions(grpLayout(), grpIdx, grpText);
        }
        grp = nullptr; grpText.clear(); grpIdx = -1;
    };
    for (int i = 0; i < hist.size(); ++i) {
        const QJsonObject e = hist[i].toObject();
        const QString role = e[QStringLiteral("role")].toString();
        const QString text = e[QStringLiteral("text")].toString();
        if (role == QLatin1String("user")) {
            closeGrp();
            auto* c = chatAppend(QStringLiteral("user"), QString(), false);
            auto* l = qobject_cast<QVBoxLayout*>(c->layout());
            buildUserCardBody(l, i, text);
            fitBubble(c, text, true);
        } else if (role == QLatin1String("assistant") || role == QLatin1String("hint")) {
            if (!grp) grp = chatAppend(QStringLiteral("assistant"), QString(), false);
            const QString vis = liveClean(text); // старые записи могут содержать маркеры
            if (vis.isEmpty()) continue;
            if (!grpText.isEmpty()) { grpLayout()->addSpacing(8); grpText += QStringLiteral("\n\n"); }
            renderRich(grpLayout(), vis);
            grpText += vis; grpIdx = i;
            fitGrow(grp, vis);
        } else if (role == QLatin1String("tool")) {
            if (!grp) grp = chatAppend(QStringLiteral("assistant"), QString(), false);
            replayCard_ = grp;
            addToolRow(e[QStringLiteral("name")].toString(), e[QStringLiteral("sub")].toString(),
                       e[QStringLiteral("ok")].toBool(true), e[QStringLiteral("detail")].toString());
            replayCard_ = nullptr;
        } else if (role == QLatin1String("error")) {
            closeGrp();
            auto* c = chatAppend(QStringLiteral("error"), QString(), false);
            auto* l = qobject_cast<QVBoxLayout*>(c->layout());
            renderRich(l, text);
            fitBubble(c, text, false);
        }
    }
    closeGrp();
}

// ============================================================================
//  сообщения чата
// ============================================================================
void MainWindow::onSend() {
    if (busyOn_) { ds_->abort(); return; }
    if (curSess_ >= 0 && sessions_[curSess_].folder.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Project folder"),
            QStringLiteral("Pick a project folder for this chat first (button in the composer row)."));
        onPickFolder();
        if (curSess_ >= 0 && sessions_[curSess_].folder.isEmpty()) return;
    }
    QString t = input_->toPlainText().trimmed();
    if (t.isEmpty() && attachPaths_.isEmpty()) return;
    input_->clear();

    if (t.compare(QStringLiteral("/compact"), Qt::CaseInsensitive) == 0) { compactNow(); return; }

    if (!attachPaths_.isEmpty()) {
        t += QStringLiteral(
            "\n\n[ATTACHED FILES — лежат в песочнице агента, полные пути:\n%1\n"
            "Прочитай через SHELL (type \"path\") или скопируй в проект при нужде.]\n")
            .arg(attachPaths_.join(QStringLiteral("\n")));
        attachPaths_.clear();
        refreshAttachUi();
    }
    lastUserText_ = t;
    agent_->sendUserMessage(t);
}

void MainWindow::onPickFolder() {
    if (curSess_ < 0) return;
    QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Project folder"),
        sessions_[curSess_].folder.isEmpty() ? cfg_.projectDir : sessions_[curSess_].folder);
    if (d.isEmpty()) return;
    sessions_[curSess_].folder = d;
    agent_->setProjectRoot(d);
    folderPill_->setText(QStringLiteral(" ") + QFileInfo(d).fileName());
    folderPill_->setToolTip(d);
    persistNow();
    logLine(QStringLiteral("Session folder: %1").arg(d));
}

void MainWindow::onBusy(bool on) {
    busyOn_ = on;
    sendBtn_->setProperty("busy", on);
    sendBtn_->style()->unpolish(sendBtn_);
    sendBtn_->style()->polish(sendBtn_);
    sendBtn_->setIcon(QIcon(fa::pix(on ? fa::xmark() : fa::arrowUp(), 12, QColor(0xff, 0xff, 0xff))));
    sendBtn_->setToolTip(on ? QStringLiteral("Stop") : QStringLiteral("Send (Ctrl+Enter)"));
    input_->setDisabled(on);
    input_->setPlaceholderText(on ? QStringLiteral("Agent is working…")
                                  : QStringLiteral("Describe the task or ask a question…"));
    if (!on) finalizeAgentCard();   // busy упал → очередь сегментов закончилась, карточку закрываем
    if (!on && titleQueued_) {
        titleQueued_ = false;
        agent_->generateTitle(titleSeed_);
    }
}

void MainWindow::addUser(const QString& text) {
    const int idx = pushHist(QStringLiteral("user"), text); // сначала, чтобы знать histIdx
    auto* c = chatAppend(QStringLiteral("user"));
    auto* l = qobject_cast<QVBoxLayout*>(c->layout());
    buildUserCardBody(l, idx, text);
    fitBubble(c, text, true);
}

void MainWindow::beginAssistant() {
    const bool fresh = !liveCard_;                 // авто-continue → продолжаем в ТОЙ ЖЕ карточке
    if (fresh) liveCard_ = chatAppend(QStringLiteral("assistant"), QStringLiteral("live"));
    else liveCard_->setState(QStringLiteral("live"));
    auto* l = qobject_cast<QVBoxLayout*>(liveCard_->layout());
    auto* sp = new Spinner(liveCard_.data());
    spinner_ = sp;
    l->addWidget(sp, 0, Qt::AlignLeft);
    if (fresh) liveCard_->setFixedWidth(120);
    liveBubble_ = nullptr;
    liveRaw_.clear();
    pulsePhase_ = 0;
    pulseTimer_.start();
}

void MainWindow::appendAssistantDelta(const QString& delta) {
    if (!liveCard_) beginAssistant();
    liveRaw_ += delta;
    if (!liveBubble_) { // пришёл первый текст — спиннер заменяем баблом
        if (spinner_ && liveCard_) {
            qobject_cast<QVBoxLayout*>(liveCard_->layout())->removeWidget(spinner_);
            delete spinner_;
            spinner_ = nullptr;
        }
        liveBubble_ = new QLabel;
        liveBubble_->setWordWrap(true);
        liveBubble_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        qobject_cast<QVBoxLayout*>(liveCard_->layout())->addWidget(liveBubble_);
    }
    liveBubble_->setText(liveClean(liveRaw_));      // ВАЖНО: маркеры/ops в стриме не видны
    fitBubble(liveCard_, liveClean(liveRaw_), false);
    smoothScrollBottom();
}

void MainWindow::endAssistant(const QString& cleanText) {
    pulseTimer_.stop();
    if (!liveCard_) return;
    SquircleCard* card = liveCard_.data();
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    if (liveBubble_) { l->removeWidget(liveBubble_); delete liveBubble_; liveBubble_ = nullptr; }
    if (spinner_)    { l->removeWidget(spinner_); delete spinner_; spinner_ = nullptr; }
    const QString vis = liveClean(cleanText); // страховка: ни одного маркера/ops в UI и в истории
    if (!vis.isEmpty()) {
        if (!groupText_.isEmpty()) { l->addSpacing(8); groupText_ += QStringLiteral("\n\n"); }
        renderRich(l, vis);
        groupText_ += vis;
        fitGrow(card, vis);
    }
    liveCard_->setState(QString()); liveCard_->setPulseColor(QColor());
    liveRaw_.clear();
    // карточка НЕ обнуляется: следующая авто-итерация продолжит в ней же;
    // финальный шаг определяется onBusy(false) → finalizeAgentCard()
    smoothScrollBottom();
}

void MainWindow::finalizeAgentCard() {
    if (!liveCard_) return;
    SquircleCard* card = liveCard_.data();
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    fitBubble(card, groupText_.isEmpty() ? QStringLiteral("…") : groupText_, false);
    if (!groupText_.isEmpty()) {
        const int idx = pushHist(QStringLiteral("assistant"), groupText_);
        addAssistantActions(l, idx, groupText_);
        if (curSess_ >= 0 && sessions_[curSess_].title == QStringLiteral("New Chat") && !titleQueued_) {
            titleQueued_ = true;
            titleSeed_ = lastUserText_ + QStringLiteral("\n---\n") + groupText_.left(1500);
        }
    }
    groupText_.clear();
    liveCard_ = nullptr;
    maybeCompact();
}

void MainWindow::addToolRow(const QString& name, const QString& sub, bool ok, const QString& detail) {
    // во время работы агента / реплея-группы — в общую карточку; иначе отдельной плашкой
    const bool inside = (liveCard_ != nullptr || replayCard_ != nullptr);
    SquircleCard* card = inside ? (liveCard_ ? liveCard_.data() : replayCard_)
                                : chatAppend(QStringLiteral("tool"), ok ? QString() : QStringLiteral("fail"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* row = new QWidget;
    row->setObjectName(QStringLiteral("toolLine"));
    auto* rl = new QVBoxLayout(row);
    rl->setContentsMargins(10, 6, 10, 6); rl->setSpacing(3);
    auto* lbl = new QLabel(QStringLiteral("<b style='color:%1'>●</b> <b style='color:#c9cad0'>%2</b>  <span style='color:#6f727c'>%3</span>")
                               .arg(ok ? QStringLiteral("#35cf7f") : QStringLiteral("#ff5470"),
                                    name.toHtmlEscaped(), sub.toHtmlEscaped()));
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QStringLiteral("font-family:'Cascadia Code','Consolas',monospace;font-size:12px;border:none;background:transparent;"));
    rl->addWidget(lbl);
    if (!detail.isEmpty()) {
        auto* det = new QLabel(QStringLiteral("<span style='color:#6a6d76;font-size:11px;font-family:Consolas,monospace;'>%1</span>")
                                   .arg(detail.left(1500).toHtmlEscaped()));
        det->setWordWrap(true);
        det->setStyleSheet(QStringLiteral("border:none;background:transparent;"));
        rl->addWidget(det);
    }
    l->addWidget(row);
    if (inside) fitGrow(card, name + QStringLiteral("  ") + sub + (detail.isEmpty() ? QString()
                     : QStringLiteral("\n") + detail.left(240)));
    else fitBubble(card, name + QStringLiteral(" ") + sub, false);
    pushHist(QStringLiteral("tool"), QString(), name, sub, ok, detail.left(400));
}

void MainWindow::addImage(const QByteArray& bytes, const QString& mime, const QString& alt) {
    Q_UNUSED(mime);
    QPixmap pm;
    if (!pm.loadFromData(bytes)) { addError(QStringLiteral("image decode failed")); return; }
    const bool inside = (liveCard_ != nullptr || replayCard_ != nullptr);
    SquircleCard* card = inside ? (liveCard_ ? liveCard_.data() : replayCard_) : chatAppend(QStringLiteral("assistant"));
    if (inside && card->width() < 276) card->setFixedWidth(276);
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());

    auto* lbl = new QLabel;
    lbl->setPixmap(roundedPm(pm.scaledToWidth(240, Qt::SmoothTransformation), 12)); // скруглённое
    lbl->setToolTip(alt.isEmpty() ? QStringLiteral("image") : alt);
    lbl->setCursor(Qt::PointingHandCursor);
    lbl->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(lbl, &QLabel::customContextMenuRequested, this, [this, lbl, bytes](const QPoint& pos) {
        QMenu m(lbl);
        QAction* aCopy = m.addAction(QStringLiteral("Copy image"));
        QAction* aSave = m.addAction(QStringLiteral("Save as…"));
        QAction* sel = m.exec(lbl->mapToGlobal(pos));
        QPixmap full; full.loadFromData(bytes);
        if (sel == aCopy) {
            QGuiApplication::clipboard()->setPixmap(full);
        } else if (sel == aSave) {
            const QString fn = QFileDialog::getSaveFileName(this, QStringLiteral("Save image"),
                QDir::homePath() + QStringLiteral("/image.png"),
                QStringLiteral("PNG (*.png);;JPEG (*.jpg)"));
            if (!fn.isEmpty()) full.save(fn);
        }
    });
    l->addWidget(lbl);
    if (!inside) card->setFixedWidth(240 + 36);
}

void MainWindow::addError(const QString& err) {
    pulseTimer_.stop();
    auto* card = chatAppend(QStringLiteral("error"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(QStringLiteral("<b style='color:#ff5470'>Error</b><br><span style='color:#ffb7c5'>%1</span>")
                               .arg(err.toHtmlEscaped()));
    lbl->setWordWrap(true);
    l->addWidget(lbl);
    fitBubble(card, err, false);
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
    d->setWindowTitle(QStringLiteral("Sandbox — session temp files"));
    d->resize(620, 460);
    auto* vl = new QVBoxLayout(d);
    vl->setContentsMargins(16, 14, 16, 14);
    vl->setSpacing(10);

    auto* hint = new QLabel(QStringLiteral(
        "Agent's temp zone: intermediate files, SANDBOX SHELL runs and tests live here. "
        "Drop your own files in (the agent will see them) or grab results. Double-click to open."));
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
                ? QStringLiteral("  (%1 KB)").arg(fi.size() / 1024) : QString();
            auto* it = new QListWidgetItem(fi.isDir()
                ? QStringLiteral("[folder] ") + fi.fileName() : fi.fileName() + size);
            it->setData(Qt::UserRole, fi.absoluteFilePath());
            list->addItem(it);
        }
        if (!list->count()) {
            auto* it = new QListWidgetItem(QStringLiteral("(empty — drop files using the button below)"));
            it->setFlags(Qt::NoItemFlags);
            list->addItem(it);
        }
    };
    refresh();

    connect(list, &QListWidget::itemDoubleClicked, d, [](QListWidgetItem* it) {
        const QString path = it->data(Qt::UserRole).toString();
        if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    auto mk = [](const QString& title, const QString& g) {
        auto* b = new QPushButton(title);
        b->setObjectName(QStringLiteral("ghostBtn"));
        b->setCursor(Qt::PointingHandCursor);
        fa::apply(b, g, QColor(0x9a, 0x9c, 0xa6));
        return b;
    };
    auto* addBtn = mk(QStringLiteral("Add files…"), fa::upload());
    auto* termBtn = mk(QStringLiteral("Terminal here"), fa::terminal());
    auto* openBtn = mk(QStringLiteral("Explorer"), fa::folder());
    auto* delBtn = mk(QStringLiteral("Delete"), fa::trash());
    auto* refBtn = mk(QStringLiteral("Refresh"), fa::refresh());
    auto* row = new QHBoxLayout; row->setSpacing(8);
    row->addWidget(addBtn); row->addWidget(termBtn); row->addWidget(openBtn);
    row->addWidget(delBtn); row->addStretch(1); row->addWidget(refBtn);
    vl->addLayout(row);

    QObject::connect(addBtn, &QPushButton::clicked, d, [d, dir, refresh]() {
        const QStringList files = QFileDialog::getOpenFileNames(d, QStringLiteral("Add files to sandbox"));
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
            QMessageBox::warning(d, QStringLiteral("Sandbox"),
                                 QStringLiteral("Could not delete: %1").arg(path));
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
    logLine(QStringLiteral("Settings applied."));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    persistNow();
    QMainWindow::closeEvent(e);
}

// ============================================================================
//  rich-рендер: текст-куски markdown + код-блоки с кнопкой Copy
// ============================================================================
void MainWindow::renderRich(QVBoxLayout* l, const QString& text) {
    static const QRegularExpression fenceRe(QStringLiteral("```(\\w*)\\n([\\s\\S]*?)(?:```|$)"));
    auto addText = [l](const QString& chunk) {
        auto* lbl = new QLabel(mdToHtml(chunk));
        lbl->setWordWrap(true);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->addWidget(lbl);
    };
    int pos = 0;
    auto it = fenceRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString pre = text.mid(pos, m.capturedStart(0) - pos).trimmed();
        if (!pre.isEmpty()) addText(pre);

        const QString code = m.captured(2).trimmed();
        const QString lang = m.captured(1);

        auto* box = new QFrame; box->setObjectName(QStringLiteral("codeBox"));
        auto* bl = new QVBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(0);

        auto* hdr = new QWidget;
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(10, 4, 6, 2);
        auto* cap = new QLabel(lang.isEmpty() ? QStringLiteral("code") : lang);
        cap->setStyleSheet(QStringLiteral("color:#7d7f89; font-size:11px;"));
        hl->addWidget(cap);
        hl->addStretch(1);
        auto* cp = new QPushButton;
        cp->setObjectName(QStringLiteral("miniBtn"));
        cp->setCursor(Qt::PointingHandCursor);
        cp->setToolTip(QStringLiteral("Copy"));
        cp->setIcon(QIcon(fa::pix(fa::copy(), 11, QColor(0x7d, 0x7f, 0x89))));
        cp->setIconSize(QSize(11, 11));
        connect(cp, &QPushButton::clicked, this, [cp, code]() {
            QGuiApplication::clipboard()->setText(code);
            cp->setIcon(QIcon(fa::pix(fa::check(), 11, QColor(0x35, 0xcf, 0x7f))));
            QTimer::singleShot(1100, cp, [cp]() {
                cp->setIcon(QIcon(fa::pix(fa::copy(), 11, QColor(0x7d, 0x7f, 0x89))));
            });
        });
        hl->addWidget(cp);
        bl->addWidget(hdr);

        auto* area = new QPlainTextEdit;
        area->setObjectName(QStringLiteral("codeArea"));
        area->setReadOnly(true);
        area->setPlainText(code);
        area->setWordWrapMode(QTextOption::NoWrap);
        const QFontMetrics fm(QFont(QStringLiteral("Consolas"), 9));
        const int lines = code.count(QLatin1Char('\n')) + 1;
        area->setFixedHeight(qBound(34, lines * fm.lineSpacing() + 22, 340));
        bl->addWidget(area);

        l->addWidget(box);
        pos = m.capturedEnd(0);
    }
    const QString tail = text.mid(pos).trimmed();
    if (!tail.isEmpty() || pos == 0) addText(tail.isEmpty() ? text : tail);
}

// ============================================================================
//  действия под ответом ассистента: Copy / Regenerate
// ============================================================================
void MainWindow::addAssistantActions(QVBoxLayout* l, int histIdx, const QString& plain) {
    auto* row = new QWidget;
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 2, 0, 0);
    rl->setSpacing(2);
    auto mk = [](const QString& txt, const QString& g) {
        auto* b = new QPushButton(txt);
        b->setObjectName(QStringLiteral("actBtn"));
        b->setCursor(Qt::PointingHandCursor);
        b->setIcon(QIcon(fa::pix(g, 10, QColor(0x6f, 0x72, 0x7c))));
        b->setIconSize(QSize(10, 10));
        return b;
    };
    auto* cpBtn = mk(QStringLiteral("Copy"), fa::copy());
    auto* rgBtn = mk(QStringLiteral("Regenerate"), fa::rotateRight());
    connect(cpBtn, &QPushButton::clicked, this, [this, cpBtn, plain]() {
        onCopyCard(plain);
        cpBtn->setIcon(QIcon(fa::pix(fa::check(), 10, QColor(0x35, 0xcf, 0x7f))));
        QTimer::singleShot(1100, cpBtn, [cpBtn]() {
            cpBtn->setIcon(QIcon(fa::pix(fa::copy(), 10, QColor(0x6f, 0x72, 0x7c))));
        });
    });
    connect(rgBtn, &QPushButton::clicked, this, [this, histIdx]() { onRegenerate(histIdx); });
    rl->addWidget(cpBtn);
    rl->addWidget(rgBtn);
    rl->addStretch(1);
    l->addWidget(row);
}

void MainWindow::onCopyCard(const QString& plain) {
    QGuiApplication::clipboard()->setText(plain);
}

// Regenerate: откат deepseek-цепочки к точке ДО того user-сообщения и повторная отправка
void MainWindow::onRegenerate(int histIdx) {
    if (busyOn_ || curSess_ < 0) return;
    auto& hist = sessions_[curSess_].history;
    if (histIdx < 0 || histIdx >= hist.size()) return;
    // ближайший user ИЛИ точка отката до него
    int j = histIdx - 1;
    while (j >= 0 && hist[j].toObject().value(QStringLiteral("role")).toString() != QLatin1String("user")) --j;
    if (j < 0) { addError(QStringLiteral("Nothing to regenerate — no user message above")); return; }
    const QString userText = hist[j].toObject().value(QStringLiteral("text")).toString();
    const QString pprev    = hist[j].toObject().value(QStringLiteral("pprev")).toString();
    // откат цепочки DeepSeek к моменту до этого хода (история тоже обрезается)
    ds_->setSession(sessions_[curSess_].dsSessionId, pprev);
    while (hist.size() > j) hist.removeLast(); // QJsonArray без resize; user-бабл пересоздастся при отправке
    persistNow();
    clearChatUi();
    replaying_ = true; replayHistory(); replaying_ = false;
    if (hist.isEmpty()) showEmptyState();
    agent_->sendUserMessage(userText);
}

// ============================================================================
//  compact: сводка → новая сессия у DeepSeek → бутстрап продолжения
// ============================================================================
void MainWindow::compactNow() {
    if (compacting_) return;
    if (curSess_ < 0) return;
    if (busyOn_) { addError(QStringLiteral("Wait for the run to finish, then /compact")); return; }
    if (sessions_[curSess_].history.isEmpty()) { addError(QStringLiteral("Nothing to compact yet")); return; }
    compacting_ = true;
    auto* card = chatAppend(QStringLiteral("tool"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* lbl = new QLabel(QStringLiteral("<b style='color:#35cf7f'>COMPACT</b> <span style='color:#6f727c'>squeezing context…</span>"));
    lbl->setStyleSheet(QStringLiteral("font-family:'Cascadia Code','Consolas',monospace;font-size:12px;"));
    l->addWidget(lbl);
    fitBubble(card, QStringLiteral("COMPACT squeezing context…"), false);
    agent_->compactSession();
}

void MainWindow::onCompactDone(const QString& summary) {
    compacting_ = false;
    if (curSess_ < 0) return;
    if (summary.isEmpty()) { addError(QStringLiteral("Compact failed — context kept as is")); return; }

    // контекст обнуляется ДО pushHist (иначе ссылка на сессию протухнет)
    sessions_[curSess_].ctxChars = summary.size();

    // новая память у DeepSeek + контракт тулов пойдёт первым сообщением заново
    ds_->newChat();
    sessions_[curSess_].dsSessionId.clear();
    sessions_[curSess_].parentMessageId.clear();
    agent_->restoreSession(false);

    auto* card = chatAppend(QStringLiteral("hint"));
    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    auto* hdr = new QLabel(QStringLiteral("<b style='color:#b8455e'>Context compacted — new DeepSeek chat</b>"));
    hdr->setWordWrap(true);
    l->addWidget(hdr);
    renderRich(l, summary);
    fitBubble(card, summary, false);
    pushHist(QStringLiteral("hint"), summary);
    persistNow();

    const QString boot = QStringLiteral(
        "Контекст пересжат из прошлой сессии. Сводка:\n\n%1\n\n"
        "Действуй строго по разделу «Следующее действие». Подтверди одним предложением и жди мою команду.").arg(summary);
    agent_->sendUserMessage(boot);
}

void MainWindow::maybeCompact() {
    if (curSess_ < 0 || compacting_) return;
    if (sessions_[curSess_].ctxChars >= 190000) { // ~200k символов — прикидка лимита контекста
        auto* card = chatAppend(QStringLiteral("hint"));
        auto* l = qobject_cast<QVBoxLayout*>(card->layout());
        auto* lbl = new QLabel(QStringLiteral("<i style='color:#b8455e'>Context limit is near — auto-compacting…</i>"));
        l->addWidget(lbl);
        fitBubble(card, QStringLiteral("Context limit is near — auto-compacting…"), false);
        compactNow();
    }
}

// ============================================================================
//  авто-тайтл сессии из первого осмысленного ответа
// ============================================================================
void MainWindow::autoTitleFromAnswer(const QString& t) {
    if (curSess_ < 0) return;
    Session& s = sessions_[curSess_];
    if (s.title != QStringLiteral("New Chat")) return;
    QString flat = t.simplified();
    flat.remove(QRegularExpression(QStringLiteral("^[#>*`\\-\\s]+")));
    const int dot = flat.indexOf(QRegularExpression(QStringLiteral("[.!?]")));
    if (dot > 8 && dot < 44) flat = flat.left(dot);
    flat = flat.left(42).trimmed();
    if (flat.size() < 3) return; // «typing…», маркер-огрызок и т.п.
    s.title = flat;
    setWindowTitle(QStringLiteral("%1 — DeepSeekIDE").arg(s.title.left(56)));
    refreshSessionList();
    persistNow();
}

// ============================================================================
//  вложения: файлы копируются в sandbox/attachments, агенту уходят пути
// ============================================================================
void MainWindow::refreshAttachUi() {
    if (!attachRow_) return;
    if (attachPaths_.isEmpty()) { attachRow_->hide(); return; }
    QStringList names;
    for (const QString& ap : attachPaths_) names << QFileInfo(ap).fileName();
    attachLbl_->setText(names.join(QStringLiteral(";  ")));
    attachRow_->show();
}

void MainWindow::onAttach() {
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Attach files"));
    if (files.isEmpty()) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/sandbox/attachments");
    QDir().mkpath(dir);
    for (const QString& f : files) {
        const QString dst = QDir(dir).filePath(QFileInfo(f).fileName());
        QFile::remove(dst);
        QFile::copy(f, dst);
        if (!attachPaths_.contains(dst)) attachPaths_ << dst;
    }
    refreshAttachUi();
    logLine(QStringLiteral("Attached %1 file(s)").arg(files.size()));
}

// ============================================================================
//  user-карточка: тело (rich) + действия Copy / Edit
// ============================================================================
void MainWindow::buildUserCardBody(QVBoxLayout* l, int histIdx, const QString& text) {
    renderRich(l, text);
    addUserActions(l, histIdx, text);
}

void MainWindow::addUserActions(QVBoxLayout* l, int histIdx, const QString& plain) {
    auto* row = new QWidget;
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 2, 0, 0);
    rl->setSpacing(2);
    auto mk = [](const QString& txt, const QString& g) {
        auto* b = new QPushButton(txt);
        b->setObjectName(QStringLiteral("actBtn"));
        b->setCursor(Qt::PointingHandCursor);
        b->setIcon(QIcon(fa::pix(g, 10, QColor(0xf0, 0xd9, 0xde))));
        b->setIconSize(QSize(10, 10));
        return b;
    };
    auto* cpBtn = mk(QStringLiteral("Copy"), fa::copy());
    auto* edBtn = mk(QStringLiteral("Edit"), fa::pen());
    connect(cpBtn, &QPushButton::clicked, this, [this, cpBtn, plain]() {
        onCopyCard(plain);
        cpBtn->setIcon(QIcon(fa::pix(fa::check(), 10, QColor(0x35, 0xcf, 0x7f))));
        QTimer::singleShot(1100, cpBtn, [cpBtn]() {
            cpBtn->setIcon(QIcon(fa::pix(fa::copy(), 10, QColor(0xf0, 0xd9, 0xde))));
        });
    });
    connect(edBtn, &QPushButton::clicked, this, [this, histIdx, edBtn]() { onEditStart(histIdx, edBtn); });
    rl->addStretch(1);      // user справа — действия жмутся к правому краю
    rl->addWidget(cpBtn);
    rl->addWidget(edBtn);
    l->addWidget(row);
}

// Edit: содержимое карточки заменяется inline-редактором; Send = перегенерация с точки
void MainWindow::onEditStart(int histIdx, QPushButton* btn) {
    if (busyOn_ || curSess_ < 0) return;
    QWidget* w = btn->parentWidget();
    while (w && !qobject_cast<SquircleCard*>(w)) w = w->parentWidget();
    auto* card = qobject_cast<SquircleCard*>(w);
    if (!card) return;
    auto& hist = sessions_[curSess_].history;
    if (histIdx < 0 || histIdx >= hist.size()) return;
    const QString curText = hist[histIdx].toObject().value(QStringLiteral("text")).toString();

    auto* l = qobject_cast<QVBoxLayout*>(card->layout());
    QLayoutItem* it;
    while ((it = l->takeAt(0)) != nullptr) { if (it->widget()) delete it->widget(); delete it; }

    card->setMinimumWidth(360);
    auto* edit = new QPlainTextEdit;
    edit->setObjectName(QStringLiteral("composer"));
    edit->setPlainText(curText);
    edit->setFixedHeight(qBound(56, (curText.count(QLatin1Char('\n')) + 1) * 20 + 16, 220));
    l->addWidget(edit);

    auto* row = new QWidget;
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 4, 0, 0);
    rl->setSpacing(6);
    auto mk = [](const QString& txt) {
        auto* b = new QPushButton(txt);
        b->setObjectName(QStringLiteral("actBtn"));
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto* cancel = mk(QStringLiteral("Cancel"));
    auto* send = mk(QStringLiteral("Send"));
    send->setStyleSheet(QStringLiteral("color:#f0cdd5;"));
    connect(cancel, &QPushButton::clicked, this, [this, l, histIdx, curText, card]() {
        QLayoutItem* it;
        while ((it = l->takeAt(0)) != nullptr) { if (it->widget()) delete it->widget(); delete it; }
        card->setMinimumWidth(0);
        buildUserCardBody(l, histIdx, curText);
    });
    connect(send, &QPushButton::clicked, this, [this, histIdx, edit]() {
        const QString nt = edit->toPlainText().trimmed();
        if (!nt.isEmpty()) onEditResend(histIdx, nt);
    });
    rl->addStretch(1);
    rl->addWidget(cancel);
    rl->addWidget(send);
    l->addWidget(row);
    edit->setFocus();
    QTextCursor tc = edit->textCursor();
    tc.movePosition(QTextCursor::End);
    edit->setTextCursor(tc);
}

// откат цепочки к точке до этого сообщения + повторная отправка редактированного текста
void MainWindow::onEditResend(int histIdx, const QString& newText) {
    if (busyOn_ || curSess_ < 0 || newText.isEmpty()) return;
    auto& hist = sessions_[curSess_].history;
    if (histIdx < 0 || histIdx >= hist.size()) return;
    if (hist[histIdx].toObject().value(QStringLiteral("role")).toString() != QLatin1String("user")) return;
    const QString pprev = hist[histIdx].toObject().value(QStringLiteral("pprev")).toString();
    ds_->setSession(sessions_[curSess_].dsSessionId, pprev);
    while (hist.size() > histIdx) hist.removeLast();
    persistNow();
    clearChatUi();
    replaying_ = true; replayHistory(); replaying_ = false;
    if (hist.isEmpty()) showEmptyState();
    lastUserText_ = newText;
    agent_->sendUserMessage(newText);
}
