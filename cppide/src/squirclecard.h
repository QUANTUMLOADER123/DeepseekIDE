#pragma once
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QColor>

// Карточка со сглаживанием superellipse (squircle, n≈4.6) вместо
// стандартных border-radius. Роли: user / assistant / tool / error / hint.
class SquircleCard : public QFrame {
    Q_OBJECT
public:
    explicit SquircleCard(const QString& role, QWidget* parent = nullptr)
        : QFrame(parent), role_(role) { setAttribute(Qt::WA_TranslucentBackground); }

    void setState(const QString& st) { state_ = st; update(); }
    QString state() const { return state_; }
    void setPulseColor(const QColor& c) { pulseColor_ = c; update(); } // рисуется при state=="live"

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(1.2, 1.2, -1.2, -1.2);
        QPainterPath path = squirclePath(r, 4.6);

        // фон
        if (role_ == QLatin1String("user")) {
            QLinearGradient g(r.topLeft(), r.bottomRight());
            g.setColorAt(0.0, QColor(52, 59, 125));
            g.setColorAt(1.0, QColor(38, 43, 88));
            p.fillPath(path, g);
        } else if (role_ == QLatin1String("assistant")) {
            p.fillPath(path, QColor(21, 25, 41));
        } else if (role_ == QLatin1String("tool")) {
            p.fillPath(path, QColor(15, 18, 28));
        } else if (role_ == QLatin1String("error")) {
            p.fillPath(path, QColor(44, 20, 29));
        } else { // hint
            p.fillPath(path, QColor(18, 21, 31));
        }

        // рамка
        QPen pen;
        pen.setWidthF(1.3);
        if (role_ == QLatin1String("user")) pen.setColor(QColor(74, 82, 184));
        else if (role_ == QLatin1String("assistant") && state_ == QLatin1String("live"))
            pen.setColor(pulseColor_.isValid() ? pulseColor_ : QColor(141, 131, 255));
        else if (role_ == QLatin1String("assistant")) pen.setColor(QColor(36, 41, 65));
        else if (role_ == QLatin1String("error")) pen.setColor(QColor(88, 36, 56));
        else if (role_ == QLatin1String("hint")) { pen.setColor(QColor(56, 64, 107)); pen.setStyle(Qt::DashLine); }
        else pen.setColor(QColor(30, 35, 52));
        p.strokePath(path, pen);

        // tool: цветная полоса слева
        if (role_ == QLatin1String("tool")) {
            QRectF bar(r.left() + 1.0, r.top() + 1.0, 3.4, r.height() - 2.0);
            QPainterPath bp = squirclePath(bar, 4.0);
            p.fillPath(bp, state_ == QLatin1String("fail") ? QColor(255, 84, 112) : QColor(53, 207, 127));
        }
    }

    // утилита: путь superellipse (нужна и снаружи — для лого/превью)
public:
    static QPainterPath squirclePath(const QRectF& r, double n) {
        // superellipse |x|^n + |y|^n = a^n, параметризовано через степенной косинус
        const double cx = r.center().x(), cy = r.center().y();
        const double a = r.width() / 2.0, b = r.height() / 2.0;
        QPainterPath path;
        const int steps = 72;
        for (int i = 0; i <= steps; ++i) {
            const double t = (double)i / steps * 2.0 * 3.14159265358979323846;
            const double ct = std::cos(t), st = std::sin(t);
            const double x = cx + a * (ct < 0 ? -1.0 : 1.0) * std::pow(std::abs(ct), 2.0 / n);
            const double y = cy + b * (st < 0 ? -1.0 : 1.0) * std::pow(std::abs(st), 2.0 / n);
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        path.closeSubpath();
        return path;
    }

private:
    QString role_, state_;
    QColor  pulseColor_;
};
