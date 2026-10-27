#include "pow.h"
#include <QJsonDocument>
#include <cstring>
#include <cmath>

extern "C" {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4200 4201 4244 4267) // wasm3 C-хедеры: zero-sized array и прочий C-шум
#endif
#include "m3_env.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

// wasm-bindgen-модуль от DeepSeek экспортирует:
//   __wbindgen_export_0(len, align)         — malloc
//   __wbindgen_add_to_stack_pointer(delta)  — стек (аллокация через отрицательный)
//   wasm_solve(sp, chalPtr, chalLen, prefixPtr, prefixLen, difficulty)
//     пишет результат по sp: [0]=i32 код (0 = не решён), [+8]=f64 ответ
//   memory

bool PowSolver::solve(const QJsonObject& chal, quint64& answer, QString& err) {
    if (wasm_.isEmpty()) { err = QStringLiteral("PoW: wasm не загружен"); return false; }

    const QString salt     = chal.value(QStringLiteral("salt")).toString();
    const QString challenge= chal.value(QStringLiteral("challenge")).toString();
    const qint64  expireAt = chal.value(QStringLiteral("expire_at")).toVariant().toLongLong();
    const quint64 difficulty = (quint64)chal.value(QStringLiteral("difficulty")).toVariant().toULongLong();
    if (salt.isEmpty() || challenge.isEmpty()) { err = QStringLiteral("PoW: пустой challenge"); return false; }

    const QByteArray cBytes = challenge.toUtf8();
    const QByteArray pBytes = (salt + QStringLiteral("_") + QString::number(expireAt) + QStringLiteral("_")).toUtf8();

    IM3Environment env = m3_NewEnvironment();
    if (!env) { err = QStringLiteral("PoW: m3_NewEnvironment failed"); return false; }
    struct Cleanup { ~Cleanup(){ if (env) m3_FreeEnvironment(env); } IM3Environment env = nullptr; } cl;

    cl.env = env;
    M3Result r;

    IM3Runtime rt = m3_NewRuntime(env, 256 * 1024, nullptr);
    if (!rt) { err = QStringLiteral("PoW: m3_NewRuntime failed"); return false; }

    IM3Module mod = nullptr;
    r = m3_ParseModule(env, &mod, reinterpret_cast<const uint8_t*>(wasm_.constData()), (u32)wasm_.size());
    if (r) { err = QStringLiteral("PoW: parse module: %1").arg(QString::fromUtf8(r)); return false; }
    r = m3_LoadModule(rt, mod);
    if (r) { err = QStringLiteral("PoW: load module: %1").arg(QString::fromUtf8(r)); return false; }

    auto findFn = [&](const char* name, IM3Function& fn) -> bool {
        M3Result rr = m3_FindFunction(&fn, rt, name);
        if (rr) err = QStringLiteral("PoW: нет экспорта %1 (%2)").arg(QString::fromUtf8(name), QString::fromUtf8(rr));
        return rr == nullptr;
    };
    IM3Function fMalloc = nullptr, fStack = nullptr, fSolve = nullptr;
    if (!findFn("__wbindgen_export_0", fMalloc) ||
        !findFn("__wbindgen_add_to_stack_pointer", fStack) ||
        !findFn("wasm_solve", fSolve)) return false;

    auto callU32 = [&](IM3Function fn, std::initializer_list<const char*> args, u32& out, const char* what) -> bool {
        QByteArrayList store;
        const char* argv[8] = {};
        int i = 0;
        for (const char* a : args) { store.push_back(QByteArray(a)); }
        i = 0;
        for (const QByteArray& a : store) { argv[i++] = a.constData(); }
        M3Result rr = m3_CallArgv(fn, (u32)store.size(), argv);
        if (rr) { err = QStringLiteral("PoW: call %1: %2").arg(QString::fromUtf8(what), QString::fromUtf8(rr)); return false; }
        rr = m3_GetResultsV(fn, &out);
        if (rr) { err = QStringLiteral("PoW: results %1: %2").arg(QString::fromUtf8(what), QString::fromUtf8(rr)); return false; }
        return true;
    };

    u32 memSize = 0;
    uint8_t* mem = m3_GetMemory(rt, &memSize, 0);
    if (!mem) { err = QStringLiteral("PoW: нет memory у модуля"); return false; }

    // challenge
    u32 cPtr = 0;
    if (!callU32(fMalloc, { QByteArray::number(cBytes.size()).constData(), "1" }, cPtr, "malloc(challenge)")) return false;
    // prefix
    u32 pPtr = 0;
    if (!callU32(fMalloc, { QByteArray::number(pBytes.size()).constData(), "1" }, pPtr, "malloc(prefix)")) return false;

    mem = m3_GetMemory(rt, &memSize, 0);  // память могла подрасти
    if (cPtr + cBytes.size() > memSize || pPtr + pBytes.size() > memSize) { err = QStringLiteral("PoW: память короче нужного"); return false; }
    std::memcpy(mem + cPtr, cBytes.constData(), (size_t)cBytes.size());
    std::memcpy(mem + pPtr, pBytes.constData(), (size_t)pBytes.size());

    // аллокация 16 байт под результат
    u32 sp = 0;
    if (!callU32(fStack, { "-16" }, sp, "stack -16")) return false;

    {
        QByteArray sSp   = QByteArray::number(sp);
        QByteArray sCp   = QByteArray::number(cPtr);
        QByteArray sCl   = QByteArray::number(cBytes.size());
        QByteArray sPp   = QByteArray::number(pPtr);
        QByteArray sPl   = QByteArray::number(pBytes.size());
        QByteArray sDiff = QByteArray::number(difficulty);
        const char* argv[] = { sSp.constData(), sCp.constData(), sCl.constData(),
                               sPp.constData(), sPl.constData(), sDiff.constData() };
        M3Result rr = m3_CallArgv(fSolve, 6, argv);
        if (rr) { err = QStringLiteral("PoW: wasm_solve: %1").arg(QString::fromUtf8(rr)); return false; }
    }

    mem = m3_GetMemory(rt, &memSize, 0);
    if (sp + 16 > memSize) { err = QStringLiteral("PoW: sp вне памяти"); return false; }
    qint32 code;
    std::memcpy(&code, mem + sp, 4);                       // little-endian i32
    double ans;
    std::memcpy(&ans, mem + sp + 8, 8);                    // little-endian f64

    { const char* backArg = "16"; m3_CallArgv(fStack, 1, &backArg); } // вернуть стек

    if (code == 0 || ans < 0 || !std::isfinite(ans)) { err = QStringLiteral("PoW: решения нет (code=%1)").arg(code); return false; }
    answer = (quint64)ans;
    return true;
}
