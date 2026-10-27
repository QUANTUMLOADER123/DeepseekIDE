#include "settings.h"
#include <QSettings>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>

static const char* DS_WASM_DEFAULT = "https://fe-static.deepseek.com/chat/static/sha3_wasm_bg.7b9ca65ddd.wasm";

namespace Cfg {

AppCfg load() {
    QSettings s("DeepSeekCppIDE", "DeepSeekCppIDE");
    AppCfg c;
    c.token      = s.value("token").toString();
    c.cookie     = s.value("cookie").toString();
    c.hifDliq    = s.value("hif_dliq").toString();
    c.hifLeim    = s.value("hif_leim").toString();
    c.wasmUrl    = s.value("wasm_url", DS_WASM_DEFAULT).toString();
    if (c.wasmUrl.isEmpty()) c.wasmUrl = DS_WASM_DEFAULT;
    c.projectDir = s.value("project_dir").toString();
    c.thinking   = s.value("thinking", false).toBool();
    c.search     = s.value("search", false).toBool();
    c.imgOn      = s.value("img_on", false).toBool();
    c.imgModel   = s.value("img_model", "magic").toString();
    c.autoLimit  = s.value("auto_limit", 12).toInt();
    c.rules      = s.value("rules").toString();
    return c;
}

void save(const AppCfg& c) {
    QSettings s("DeepSeekCppIDE", "DeepSeekCppIDE");
    s.setValue("token", c.token);
    s.setValue("cookie", c.cookie);
    s.setValue("hif_dliq", c.hifDliq);
    s.setValue("hif_leim", c.hifLeim);
    s.setValue("wasm_url", c.wasmUrl);
    s.setValue("project_dir", c.projectDir);
    s.setValue("thinking", c.thinking);
    s.setValue("search", c.search);
    s.setValue("img_on", c.imgOn);
    s.setValue("img_model", c.imgModel);
    s.setValue("auto_limit", c.autoLimit);
    s.setValue("rules", c.rules);
}

} // namespace Cfg

SettingsDialog::SettingsDialog(AppCfg& c, QWidget* parent) : QDialog(parent), c_(c) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(560);
    auto* lay = new QFormLayout(this);

    auto* token = new QLineEdit(c_.token); token->setEchoMode(QLineEdit::Password);
    token->setPlaceholderText(tr("localStorage.token / userToken"));
    lay->addRow(tr("DeepSeek token:"), token);

    auto* hint = new QLabel(tr("chat.deepseek.com → F12 → Console: localStorage.getItem('userToken') || localStorage.getItem('token')\n"
                               "Cookies: Application tab -> Cookies -> chat.deepseek.com (ds_session_id, smidV2)"));
    hint->setWordWrap(true); hint->setStyleSheet("color:#888;font-size:12px");
    lay->addRow(hint);

    auto* cookie = new QLineEdit(c_.cookie);
    cookie->setPlaceholderText("ds_session_id=…; smidV2=…");
    lay->addRow(tr("Cookie:"), cookie);

    auto* wasm = new QLineEdit(c_.wasmUrl);
    lay->addRow(tr("PoW WASM URL:"), wasm);

    auto* folder = new QLineEdit(c_.projectDir);
    auto* pick = new QPushButton(tr("Browse…"));
    auto* frow = new QWidget; auto* fh = new QFormLayout(frow); fh->setContentsMargins(0,0,0,0);
    fh->addRow(folder, pick);
    connect(pick, &QPushButton::clicked, this, [this, folder]() {
        QString d = QFileDialog::getExistingDirectory(this, tr("Project folder"), folder->text());
        if (!d.isEmpty()) folder->setText(d);
    });
    lay->addRow(tr("Project folder:"), frow);

    auto* thinking = new QCheckBox(tr("Deep Think (reasoning)")); thinking->setChecked(c_.thinking);
    auto* search = new QCheckBox(tr("Web search")); search->setChecked(c_.search);
    lay->addRow(thinking); lay->addRow(search);

    lay->addRow(new QLabel(QStringLiteral("— %1 —").arg(tr("Images (subnp.com, free)"))));
    auto* imgOn = new QCheckBox(tr("[EXPERIMENTAL] Image generation")); imgOn->setChecked(c_.imgOn);
    auto* imgModel = new QComboBox; imgModel->addItems({"magic", "flux"});
    imgModel->setCurrentText(c_.imgModel == "flux" ? "flux" : "magic");
    lay->addRow(imgOn);
    lay->addRow(tr("Model:"), imgModel);

    auto* lim = new QSpinBox; lim->setRange(0, 100); lim->setValue(c_.autoLimit);
    lim->setToolTip(tr("Auto-continue cap; 0 = unlimited"));
    lay->addRow(tr("Auto steps in a row:"), lim);

    auto* rules = new QPlainTextEdit(c_.rules); rules->setMaximumHeight(120);
    lay->addRow(tr("Custom rules:"), rules);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (exec() == Accepted) {
        c_.token = token->text().trimmed();
        c_.cookie = cookie->text().trimmed();
        c_.wasmUrl = wasm->text().trimmed();
        c_.projectDir = folder->text().trimmed();
        c_.thinking = thinking->isChecked();
        c_.search = search->isChecked();
        c_.imgOn = imgOn->isChecked();
        c_.imgModel = imgModel->currentText();
        c_.autoLimit = lim->value();
        c_.rules = rules->toPlainText();
        Cfg::save(c_);
    }
}
