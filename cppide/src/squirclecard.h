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
        const QRectF r = rect().adjusted(1.0, 1.0, -1.0, -1.0);
        const double rad = 12.0;
        QPainterPath path = roundPath(r, rad);

        // фон
        if (role_ == QLatin1String("user")) {
            QLinearGradient g(r.topLeft(), r.bottomRight());
            g.setColorAt(0.0, QColor(143, 43, 65));
            g.setColorAt(1.0, QColor(108, 31, 48));
            p.fillPath(path, g);
        } else if (role_ == QLatin1String("assistant")) {
            p.fillPath(path, QColor(19, 20, 23));
        } else if (role_ == QLatin1String("tool")) {
            p.fillPath(path, QColor(16, 17, 19));
        } else if (role_ == QLatin1String("error")) {
            p.fillPath(path, QColor(43, 18, 24));
        } else { // hint
            p.fillPath(path, QColor(19, 20, 23));
        }

        // рамка
        QPen pen;
        pen.setWidthF(1.2);
        if (role_ == QLatin1String("user")) pen.setColor(QColor(176, 87, 106));
        else if (role_ == QLatin1String("assistant") && state_ == QLatin1String("live"))
            pen.setColor(pulseColor_.isValid() ? pulseColor_ : QColor(192, 85, 112));
        else if (role_ == QLatin1String("assistant")) pen.setColor(QColor(47, 48, 54));
        else if (role_ == QLatin1String("error")) pen.setColor(QColor(96, 34, 48));
        else if (role_ == QLatin1String("hint")) { pen.setColor(QColor(78, 80, 88)); pen.setStyle(Qt::DashLine); }
        else pen.setColor(QColor(38, 39, 44));
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
