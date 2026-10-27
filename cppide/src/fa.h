#pragma once
// Font Awesome 6 Free (Solid) helper. Шрифт: assets/fa-solid-900.ttf
#include <QFontDatabase>
#include <QPixmap>
#include <QPainter>
#include <QColor>
#include <QPushButton>
#include <QFile>
#include <QCoreApplication>

namespace fa {

inline QString family() {
    static QString fam = [] {
        QString path = QStringLiteral("assets/fa-solid-900.ttf");
        if (!QFile::exists(path))
            path = QCoreApplication::applicationDirPath() + QStringLiteral("/assets/fa-solid-900.ttf");
        const int id = QFontDatabase::addApplicationFont(path);
        const auto fams = QFontDatabase::applicationFontFamilies(id);
        return fams.isEmpty() ? QStringLiteral("Arial") : fams.first();
    }();
    return fam;
}

inline QString glyph(ushort cp) { return QString(QChar(cp)); }

// === нужные иконки (codepoints FA 6 Solid) ===
inline QString plus()        { return glyph(0x002b); }
inline QString comment()     { return glyph(0xf075); }  // сообщение/сессия
inline QString brain()       { return glyph(0xf5dc); }  // Deep Think
inline QString search()      { return glyph(0xf002); }  // поиск
inline QString image()       { return glyph(0xf03e); }  // картинки
inline QString arrowUp()     { return glyph(0xf062); }  // отправить
inline QString xmark()       { return glyph(0xf00d); }  // стоп/закрыть
inline QString gear()        { return glyph(0xf013); }  // настройки
inline QString box()         { return glyph(0xf466); }  // песочница
inline QString folder()      { return glyph(0xf07c); }  // folder-open
inline QString terminal()    { return glyph(0xf120); }
inline QString upload()      { return glyph(0xf093); }
inline QString trash()       { return glyph(0xf1f8); }
inline QString refresh()     { return glyph(0xf021); }  // arrows-rotate
inline QString winMin()      { return glyph(0xf2d1); }
inline QString winMax()      { return glyph(0xf2d0); }
inline QString winRestore()  { return glyph(0xf2d2); }
inline QString sparkles()    { return glyph(0xe2ca); }  // wand-magic-sparkles
inline QString stop()        { return glyph(0xf04d); }

inline QPixmap pix(const QString& g, int px = 14, const QColor& c = QColor(0xe6, 0xe7, 0xea)) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f(family());
    f.setPixelSize(px);
    p.setFont(f);
    p.setPen(c);
    p.drawText(pm.rect(), Qt::AlignCenter, g);
    return pm;
}

inline void apply(QPushButton* btn, const QString& g, const QColor& c, int px = 13) {
    btn->setIcon(QIcon(pix(g, px, c)));
    btn->setIconSize(QSize(px, px));
    btn->setText(QStringLiteral(" ") + btn->text());
}

} // namespace fa
