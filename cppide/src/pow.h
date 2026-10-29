#pragma once
#include <QByteArray>
#include <QString>
#include <utility>
#include <QJsonObject>

// Решатель PoW-челленджа DeepSeek Web: гоняем их же WASM (sha3_wasm)
// во встроенном интерпретаторе wasm3. WASM скачивается один раз за запуск.
class PowSolver {
public:
    void setWasm(QByteArray bytes) { wasm_ = std::move(bytes); }
    bool hasWasm() const { return !wasm_.isEmpty(); }

    // challenge: {salt, expire_at, challenge, difficulty, ...}
    // вернёт answer (u64). Ошибка попадает в err.
    bool solve(const QJsonObject& chal, quint64& answer, QString& err);

private:
    QByteArray wasm_;
};
