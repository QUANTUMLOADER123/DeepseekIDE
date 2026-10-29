#pragma once
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QColor>

// Карточка сообщения с аккуратными скруглениями (обычный rounded rect,
// класс исторически назван SquircleCard). Роли: user / assistant / tool / error / hint.
class SquircleCard : public QFrame {
    Q_OBJECT
public:
    explicit SquircleCard(const QString& role, QWidget* parent = nullptr)
        : QFrame(parent), role_(role) { setAttribute(Qt::WA_TranslucentBackground); }

    void setState(const QString& st) { state_ = st; update(); }
    QString state() const { return state_; }
    QString role() const { return role_; }
    void setPulseColor(const QColor& c) { pulseColor_ = c; update(); } // рисуется при state=="live"

    // универсальный скруглённый путь (иконки, лого, превью)
    static QPainterPath roundPath(const QRectF& r, double radius = 10.0) {
        QPainterPath p;
        p.addRoundedRect(r, radius, radius);
        return p;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        const QRectF r = rect().adjusted(1.0, 1.0, -1.0, -1.0);
        const double rad = 14.0;
        QPainterPath path = roundPath(r, rad);

        // фон (assistant — прозрачный, как на deepseek.com; user — плоская плашка)
        if (role_ == QLatin1String("error")) {
            p.fillPath(path, QColor(255, 84, 112, 16));
        } else { // user / assistant / tool / hint — единая панель сообщений #2C2C2E
            p.fillPath(path, QColor(0x2C, 0x2C, 0x2E));
        }

        // рамка
        QPen pen;
        pen.setWidthF(1.2);
        if (role_ == QLatin1String("assistant") && state_ == QLatin1String("live"))
            pen.setColor(pulseColor_.isValid() ? pulseColor_ : QColor(86, 134, 254));
        else if (role_ == QLatin1String("error")) pen.setColor(QColor(255, 84, 112, 110));
        else if (role_ == QLatin1String("hint")) { pen.setColor(QColor(0x28, 0x28, 0x29)); pen.setStyle(Qt::DashLine); }
        else pen.setColor(QColor(0x28, 0x28, 0x29));   // обводки #282829
        p.strokePath(path, pen);

        // tool: цветная полоса слева
        if (role_ == QLatin1String("tool")) {
            QRectF bar(r.left() + 1.0, r.top() + r.height() / 2.0 - 10, 3.0, 20);
            QPainterPath bp = roundPath(bar, 1.5);
            p.fillPath(bp, state_ == QLatin1String("fail") ? QColor(255, 84, 112) : QColor(53, 207, 127));
        }
    }

private:
    QString role_, state_;
    QColor  pulseColor_;
};
