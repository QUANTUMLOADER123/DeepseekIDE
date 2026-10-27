#pragma once
#include <QString>
#include <QDialog>

// ---------- настройки приложения (QSettings INI в AppData/DeepSeekCppIDE)
struct AppCfg {
    QString token;      // localStorage.token с chat.deepseek.com
    QString cookie;     // ds_session_id=…; smidV2=… (все куки deepseek.com)
    QString hifDliq;    // localStorage.hif_dliq (опционально)
    QString hifLeim;    // localStorage.hif_leim (опционально)
    QString wasmUrl;    // URL wasm-модуля PoW
    QString projectDir; // корень проекта
    bool    thinking = false;   // DeepThink
    bool    search   = false;   // поиск в вкл
    bool    imgOn    = false;   // генерация картинок
    QString imgModel = "magic"; // magic | flux (subnp.com free)
    int     autoLimit = 12;     // потолок авто-continue (0 = без лимита)
    QString rules;              // начальный промпт пользователя (rules)
};

namespace Cfg {
    AppCfg load();
    void   save(const AppCfg& c);
}

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(AppCfg& c, QWidget* parent = nullptr);
private:
    AppCfg& c_;
};
