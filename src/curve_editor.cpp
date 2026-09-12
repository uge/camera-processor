#include "curve_editor.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

CurveEditor::CurveEditor(QWidget* parent)
    : QWidget(parent), m_lut(256, 0) {
    setMinimumSize(220, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    resetLinear();
}

void CurveEditor::resetLinear() {
    m_points = { QPointF(0.0, 0.0), QPointF(1.0, 1.0) };
    m_draggedPointIdx = -1;
    rebuildLut();
    update();
}

void CurveEditor::presetSCurve() {
    m_points = {
        QPointF(0.0, 0.0),
        QPointF(0.25, 0.15),
        QPointF(0.75, 0.85),
        QPointF(1.0, 1.0)
    };
    m_draggedPointIdx = -1;
    rebuildLut();
    update();
}

void CurveEditor::presetLiftShadows() {
    m_points = {
        QPointF(0.0, 0.0),
        QPointF(0.25, 0.35),
        QPointF(0.60, 0.68),
        QPointF(1.0, 1.0)
    };
    m_draggedPointIdx = -1;
    rebuildLut();
    update();
}

void CurveEditor::setBlackPoint(double inX, double outY) {
    inX = std::clamp(inX, 0.0, 0.9);
    outY = std::clamp(outY, 0.0, 1.0);
    // Replace or set the leftmost point
    if (!m_points.empty()) {
        m_points.front() = QPointF(inX, outY);
    } else {
        m_points.push_back(QPointF(inX, outY));
    }
    // Ensure last point exists and is greater than inX
    if (m_points.size() < 2 || m_points.back().x() <= inX) {
        if (m_points.size() >= 2) m_points.pop_back();
        m_points.push_back(QPointF(1.0, 1.0));
    }
    m_draggedPointIdx = -1;
    rebuildLut();
    update();
}

void CurveEditor::setWhitePoint(double inX, double outY) {
    inX = std::clamp(inX, 0.1, 1.0);
    outY = std::clamp(outY, 0.0, 1.0);
    // Replace or set the rightmost point
    if (!m_points.empty()) {
        m_points.back() = QPointF(inX, outY);
    } else {
        m_points.push_back(QPointF(inX, outY));
    }
    // Ensure first point exists and is less than inX
    if (m_points.size() < 2 || m_points.front().x() >= inX) {
        if (m_points.size() >= 2) m_points.erase(m_points.begin());
        m_points.insert(m_points.begin(), QPointF(0.0, 0.0));
    }
    m_draggedPointIdx = -1;
    rebuildLut();
    update();
}

void CurveEditor::setGreyPoint(double inX, double outY) {
    inX = std::clamp(inX, 0.05, 0.95);
    outY = std::clamp(outY, 0.0, 1.0);
    // Find if an interior point is close, or insert a middle point
    bool replaced = false;
    for (size_t i = 1; i + 1 < m_points.size(); ++i) {
        if (std::abs(m_points[i].x() - inX) < 0.2) {
            m_points[i] = QPointF(inX, outY);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_points.push_back(QPointF(inX, outY));
    }
    m_draggedPointIdx = -1;
    rebuildLut();
    update();
}

std::vector<uint8_t> CurveEditor::getLut() const {
    return m_lut;
}

// Catmull-Rom / Monotone cubic interpolation across sorted control points
void CurveEditor::rebuildLut() {
    if (m_points.empty()) return;

    // Ensure sorted by x
    std::sort(m_points.begin(), m_points.end(), [](const QPointF& a, const QPointF& b) {
        return a.x() < b.x();
    });

    int n = static_cast<int>(m_points.size());
    std::vector<double> x(n), y(n);
    for (int i = 0; i < n; ++i) {
        x[i] = m_points[i].x();
        y[i] = m_points[i].y();
    }

    // Monotone cubic spline (Fritsch-Carlson)
    std::vector<double> deltas(n - 1), m(n, 0.0);
    for (int i = 0; i < n - 1; ++i) {
        double dx = x[i + 1] - x[i];
        deltas[i] = (dx > 1e-6) ? (y[i + 1] - y[i]) / dx : 0.0;
    }

    m[0] = deltas[0];
    for (int i = 1; i < n - 1; ++i) {
        m[i] = (deltas[i - 1] + deltas[i]) * 0.5;
    }
    m[n - 1] = deltas[n - 2];

    for (int i = 0; i < n - 1; ++i) {
        if (std::abs(deltas[i]) < 1e-6) {
            m[i] = 0.0;
            m[i + 1] = 0.0;
        } else {
            double alpha = m[i] / deltas[i];
            double beta = m[i + 1] / deltas[i];
            if (alpha < 0.0) m[i] = 0.0;
            if (beta < 0.0) m[i + 1] = 0.0;
            if (alpha * alpha + beta * beta > 9.0) {
                double tau = 3.0 / std::sqrt(alpha * alpha + beta * beta);
                m[i] = tau * alpha * deltas[i];
                m[i + 1] = tau * beta * deltas[i];
            }
        }
    }

    // Fill 256 LUT entries
    for (int i = 0; i < 256; ++i) {
        double tX = i / 255.0;
        double outY = 0.0;

        if (tX <= x[0]) {
            outY = y[0];
        } else if (tX >= x[n - 1]) {
            outY = y[n - 1];
        } else {
            int seg = 0;
            for (int k = 0; k < n - 1; ++k) {
                if (tX >= x[k] && tX <= x[k + 1]) {
                    seg = k;
                    break;
                }
            }

            double h = x[seg + 1] - x[seg];
            if (h > 1e-6) {
                double t = (tX - x[seg]) / h;
                double h00 = (1 + 2 * t) * (1 - t) * (1 - t);
                double h10 = t * (1 - t) * (1 - t);
                double h01 = t * t * (3 - 2 * t);
                double h11 = t * t * (t - 1);
                outY = h00 * y[seg] + h10 * h * m[seg] + h01 * y[seg + 1] + h11 * h * m[seg + 1];
            } else {
                outY = y[seg];
            }
        }

        outY = std::clamp(outY, 0.0, 1.0);
        m_lut[i] = static_cast<uint8_t>(std::round(outY * 255.0));
    }

    emit curveChanged(m_lut);
}

QPointF CurveEditor::widgetToNormalized(const QPoint& p) const {
    int pad = 12;
    int w = width() - 2 * pad;
    int h = height() - 2 * pad;
    if (w <= 0 || h <= 0) return QPointF(0, 0);

    double nx = std::clamp(static_cast<double>(p.x() - pad) / w, 0.0, 1.0);
    double ny = std::clamp(1.0 - static_cast<double>(p.y() - pad) / h, 0.0, 1.0);
    return QPointF(nx, ny);
}

QPoint CurveEditor::normalizedToWidget(const QPointF& p) const {
    int pad = 12;
    int w = width() - 2 * pad;
    int h = height() - 2 * pad;
    int x = pad + static_cast<int>(std::round(p.x() * w));
    int y = pad + static_cast<int>(std::round((1.0 - p.y()) * h));
    return QPoint(x, y);
}

int CurveEditor::findNearestPoint(const QPointF& normalizedPos, double threshold) const {
    int nearest = -1;
    double minDist = threshold;
    for (size_t i = 0; i < m_points.size(); ++i) {
        double dx = m_points[i].x() - normalizedPos.x();
        double dy = m_points[i].y() - normalizedPos.y();
        double d = std::sqrt(dx * dx + dy * dy);
        if (d < minDist) {
            minDist = d;
            nearest = static_cast<int>(i);
        }
    }
    return nearest;
}

void CurveEditor::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPointF norm = widgetToNormalized(event->pos());
        int idx = findNearestPoint(norm);
        if (idx >= 0) {
            m_draggedPointIdx = idx;
        } else {
            // Add a new point
            m_points.push_back(norm);
            std::sort(m_points.begin(), m_points.end(), [](const QPointF& a, const QPointF& b) {
                return a.x() < b.x();
            });
            m_draggedPointIdx = findNearestPoint(norm);
            rebuildLut();
            update();
        }
    } else if (event->button() == Qt::RightButton) {
        // Remove point if not first or last
        QPointF norm = widgetToNormalized(event->pos());
        int idx = findNearestPoint(norm);
        if (idx > 0 && idx < static_cast<int>(m_points.size()) - 1) {
            m_points.erase(m_points.begin() + idx);
            m_draggedPointIdx = -1;
            rebuildLut();
            update();
        }
    }
}

void CurveEditor::mouseMoveEvent(QMouseEvent* event) {
    if (m_draggedPointIdx >= 0 && (event->buttons() & Qt::LeftButton)) {
        QPointF norm = widgetToNormalized(event->pos());
        int n = static_cast<int>(m_points.size());

        // First point fixed at x=0, last point fixed at x=1
        if (m_draggedPointIdx == 0) {
            m_points[0] = QPointF(0.0, norm.y());
        } else if (m_draggedPointIdx == n - 1) {
            m_points[n - 1] = QPointF(1.0, norm.y());
        } else {
            // Interior points cannot cross neighbors
            double minX = m_points[m_draggedPointIdx - 1].x() + 0.02;
            double maxX = m_points[m_draggedPointIdx + 1].x() - 0.02;
            double clampedX = std::clamp(norm.x(), minX, maxX);
            m_points[m_draggedPointIdx] = QPointF(clampedX, norm.y());
        }

        rebuildLut();
        update();
    }
}

void CurveEditor::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_draggedPointIdx = -1;
    }
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent* event) {
    // Double click resets linear
    if (event->button() == Qt::LeftButton) {
        resetLinear();
    }
}

void CurveEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int pad = 12;
    QRect r(pad, pad, width() - 2 * pad, height() - 2 * pad);

    // Background
    p.fillRect(rect(), QColor(30, 30, 30));
    p.fillRect(r, QColor(40, 40, 40));

    // Grid lines (quarter divisions)
    p.setPen(QPen(QColor(60, 60, 60), 1, Qt::DashLine));
    for (int i = 1; i < 4; ++i) {
        int gx = r.left() + (r.width() * i) / 4;
        int gy = r.top() + (r.height() * i) / 4;
        p.drawLine(gx, r.top(), gx, r.bottom());
        p.drawLine(r.left(), gy, r.right(), gy);
    }

    // Linear diagonal reference line
    p.setPen(QPen(QColor(80, 80, 80), 1, Qt::DotLine));
    p.drawLine(r.bottomLeft(), r.topRight());

    // Border
    p.setPen(QPen(QColor(90, 90, 90), 1));
    p.drawRect(r);

    // Render LUT curve
    if (!m_lut.empty()) {
        QPainterPath path;
        for (int i = 0; i < 256; ++i) {
            double nx = i / 255.0;
            double ny = m_lut[i] / 255.0;
            QPoint pt = normalizedToWidget(QPointF(nx, ny));
            if (i == 0) {
                path.moveTo(pt);
            } else {
                path.lineTo(pt);
            }
        }
        p.setPen(QPen(QColor(0, 180, 255), 2));
        p.drawPath(path);
    }

    // Control points
    for (size_t i = 0; i < m_points.size(); ++i) {
        QPoint pt = normalizedToWidget(m_points[i]);
        if (static_cast<int>(i) == m_draggedPointIdx) {
            p.setBrush(QColor(255, 200, 0));
            p.setPen(QPen(Qt::white, 2));
            p.drawEllipse(pt, 6, 6);
        } else {
            p.setBrush(QColor(0, 210, 255));
            p.setPen(QPen(Qt::black, 1.5));
            p.drawEllipse(pt, 5, 5);
        }
    }
}
