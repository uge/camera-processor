#include "crop_overlay.h"

#include <QPainter>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

CropOverlay::CropOverlay(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    resetCrop();
}

void CropOverlay::setSampleMode(SampleMode mode) {
    m_sampleMode = mode;
    if (m_sampleMode != SampleNone) {
        setCursor(Qt::CrossCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
    update();
}

void CropOverlay::setTargetAspectRatio(double ratio) {
    if (ratio > 0.05 && std::abs(m_targetAspect - ratio) > 1e-4) {
        m_targetAspect = ratio;
        resetCrop();
    }
}

void CropOverlay::setSourceAspectRatio(double ratio) {
    if (ratio > 0.05 && std::abs(m_sourceAspect - ratio) > 1e-4) {
        m_sourceAspect = ratio;
        resetCrop();
    }
}

void CropOverlay::resetCrop() {
    // Fit target aspect ratio into [0, 0, 1, 1] normalized frame
    // normalized frame width = 1.0, height = 1.0, but pixel aspect ratio is m_sourceAspect.
    // In physical aspect space: norm_w * m_sourceAspect / norm_h = m_targetAspect
    // => norm_w / norm_h = m_targetAspect / m_sourceAspect =: R
    double R = m_targetAspect / m_sourceAspect;
    double w = 1.0;
    double h = 1.0;
    if (R > 1.0) {
        // Target is wider than source: height is constrained
        w = 1.0;
        h = 1.0 / R;
    } else {
        // Target is taller/narrower than source: width is constrained
        w = R;
        h = 1.0;
    }
    double x = (1.0 - w) * 0.5;
    double y = (1.0 - h) * 0.5;
    m_cropRect = QRectF(x, y, w, h);
    update();
    emit cropChanged(m_cropRect);
}

QRectF CropOverlay::getVideoRectInWidget() const {
    double widgetW = width();
    double widgetH = height();
    if (widgetW <= 0 || widgetH <= 0) return QRectF();

    double widgetAspect = widgetW / widgetH;
    double vx = 0, vy = 0, vw = widgetW, vh = widgetH;

    if (widgetAspect > m_sourceAspect) {
        // Pillarbox
        vw = widgetH * m_sourceAspect;
        vh = widgetH;
        vx = (widgetW - vw) * 0.5;
        vy = 0;
    } else {
        // Letterbox
        vw = widgetW;
        vh = widgetW / m_sourceAspect;
        vx = 0;
        vy = (widgetH - vh) * 0.5;
    }
    return QRectF(vx, vy, vw, vh);
}

QRectF CropOverlay::normalizedToWidget(const QRectF& normRect) const {
    QRectF vRect = getVideoRectInWidget();
    return QRectF(
        vRect.left() + normRect.x() * vRect.width(),
        vRect.top() + normRect.y() * vRect.height(),
        normRect.width() * vRect.width(),
        normRect.height() * vRect.height()
    );
}

QRectF CropOverlay::widgetToNormalized(const QRectF& wRect) const {
    QRectF vRect = getVideoRectInWidget();
    if (vRect.width() <= 0 || vRect.height() <= 0) return QRectF(0, 0, 1, 1);
    return QRectF(
        (wRect.x() - vRect.left()) / vRect.width(),
        (wRect.y() - vRect.top()) / vRect.height(),
        wRect.width() / vRect.width(),
        wRect.height() / vRect.height()
    );
}

CropOverlay::Handle CropOverlay::hitTest(const QPoint& pos) const {
    QRectF r = normalizedToWidget(m_cropRect);
    const double tol = 12.0;

    // Corners
    if (std::hypot(pos.x() - r.left(), pos.y() - r.top()) <= tol) return TopLeft;
    if (std::hypot(pos.x() - r.right(), pos.y() - r.top()) <= tol) return TopRight;
    if (std::hypot(pos.x() - r.left(), pos.y() - r.bottom()) <= tol) return BottomLeft;
    if (std::hypot(pos.x() - r.right(), pos.y() - r.bottom()) <= tol) return BottomRight;

    // Edges
    if (std::abs(pos.y() - r.top()) <= tol && pos.x() >= r.left() && pos.x() <= r.right()) return Top;
    if (std::abs(pos.y() - r.bottom()) <= tol && pos.x() >= r.left() && pos.x() <= r.right()) return Bottom;
    if (std::abs(pos.x() - r.left()) <= tol && pos.y() >= r.top() && pos.y() <= r.bottom()) return Left;
    if (std::abs(pos.x() - r.right()) <= tol && pos.y() >= r.top() && pos.y() <= r.bottom()) return Right;

    // Inside rect
    if (r.contains(pos)) return Move;

    return None;
}

void CropOverlay::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF vRect = getVideoRectInWidget();
    QRectF cropW = normalizedToWidget(m_cropRect);

    // Dim regions outside video frame (pillarbox / letterbox margins)
    QRegion widgetRegion(rect());
    QRegion videoRegion(vRect.toRect());
    p.setClipRegion(widgetRegion.subtracted(videoRegion));
    p.fillRect(rect(), QColor(0, 0, 0, 200));

    // Dim region outside crop but inside video frame
    QRegion cropRegion(cropW.toRect());
    p.setClipRegion(videoRegion.subtracted(cropRegion));
    p.fillRect(vRect, QColor(0, 0, 0, 110));

    p.setClipping(false);

    // Crop box outline
    p.setPen(QPen(QColor(64, 180, 255), 2, Qt::SolidLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cropW);

    // Rule of thirds grid lines inside crop box
    p.setPen(QPen(QColor(255, 255, 255, 80), 1, Qt::DashLine));
    double thirdW = cropW.width() / 3.0;
    double thirdH = cropW.height() / 3.0;
    p.drawLine(QPointF(cropW.left() + thirdW, cropW.top()), QPointF(cropW.left() + thirdW, cropW.bottom()));
    p.drawLine(QPointF(cropW.left() + 2 * thirdW, cropW.top()), QPointF(cropW.left() + 2 * thirdW, cropW.bottom()));
    p.drawLine(QPointF(cropW.left(), cropW.top() + thirdH), QPointF(cropW.right(), cropW.top() + thirdH));
    p.drawLine(QPointF(cropW.left(), cropW.top() + 2 * thirdH), QPointF(cropW.right(), cropW.top() + 2 * thirdH));

    // Corner handle brackets
    p.setPen(QPen(QColor(255, 255, 255), 3));
    double bLen = std::min(14.0, std::min(cropW.width(), cropW.height()) * 0.2);

    // Top-Left
    p.drawLine(cropW.topLeft(), cropW.topLeft() + QPointF(bLen, 0));
    p.drawLine(cropW.topLeft(), cropW.topLeft() + QPointF(0, bLen));
    // Top-Right
    p.drawLine(cropW.topRight(), cropW.topRight() - QPointF(bLen, 0));
    p.drawLine(cropW.topRight(), cropW.topRight() + QPointF(0, bLen));
    // Bottom-Left
    p.drawLine(cropW.bottomLeft(), cropW.bottomLeft() + QPointF(bLen, 0));
    p.drawLine(cropW.bottomLeft(), cropW.bottomLeft() - QPointF(0, bLen));
    // Bottom-Right
    p.drawLine(cropW.bottomRight(), cropW.bottomRight() - QPointF(bLen, 0));
    p.drawLine(cropW.bottomRight(), cropW.bottomRight() - QPointF(0, bLen));

    if (m_sampleMode != SampleNone) {
        QString modeStr = (m_sampleMode == SampleWhite) ? "White Point" :
                          (m_sampleMode == SampleBlack) ? "Black Point" : "Grey Point";
        QString banner = QString("Sampling %1: Click anywhere on image").arg(modeStr);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 180));
        QRect bannerRect(10, 10, 320, 30);
        p.drawRoundedRect(bannerRect, 5, 5);
        p.setPen(QColor(255, 220, 60));
        p.drawText(bannerRect, Qt::AlignCenter, banner);
    }
}

void CropOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_sampleMode != SampleNone) {
            QRectF vRect = getVideoRectInWidget();
            if (vRect.contains(event->pos())) {
                double normX = (event->pos().x() - vRect.left()) / vRect.width();
                double normY = (event->pos().y() - vRect.top()) / vRect.height();
                normX = std::clamp(normX, 0.0, 1.0);
                normY = std::clamp(normY, 0.0, 1.0);
                emit samplePointClicked(m_sampleMode, QPointF(normX, normY));
            }
            setSampleMode(SampleNone);
            event->accept();
            return;
        }

        m_activeHandle = hitTest(event->pos());
        m_dragStartPos = event->pos();
        m_dragStartCropRect = m_cropRect;
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void CropOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (m_sampleMode != SampleNone) {
        setCursor(Qt::CrossCursor);
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_activeHandle != None) {
        applyDrag(event->pos());
        update();
        emit cropChanged(m_cropRect);
        event->accept();
        return;
    }

    // Update cursor based on hover
    Handle h = hitTest(event->pos());
    switch (h) {
        case TopLeft:
        case BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case TopRight:
        case BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Top:
        case Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case Left:
        case Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case Move:
            setCursor(Qt::SizeAllCursor);
            break;
        default:
            setCursor(Qt::ArrowCursor);
            break;
    }
    QWidget::mouseMoveEvent(event);
}

void CropOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_activeHandle != None) {
        m_activeHandle = None;
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void CropOverlay::resizeEvent(QResizeEvent* /*event*/) {
    update();
}

void CropOverlay::applyDrag(const QPoint& curPos) {
    QRectF vRect = getVideoRectInWidget();
    if (vRect.width() <= 0 || vRect.height() <= 0) return;

    double dx = (curPos.x() - m_dragStartPos.x()) / vRect.width();
    double dy = (curPos.y() - m_dragStartPos.y()) / vRect.height();

    // The ratio of normalized width to normalized height must maintain:
    // (normW * m_sourceAspect) / normH = m_targetAspect
    // => normW / normH = m_targetAspect / m_sourceAspect =: R
    double R = m_targetAspect / m_sourceAspect;

    if (m_activeHandle == Move) {
        double newX = m_dragStartCropRect.x() + dx;
        double newY = m_dragStartCropRect.y() + dy;

        // Clamp inside [0.0, 1.0]
        newX = std::max(0.0, std::min(1.0 - m_dragStartCropRect.width(), newX));
        newY = std::max(0.0, std::min(1.0 - m_dragStartCropRect.height(), newY));

        m_cropRect.moveLeft(newX);
        m_cropRect.moveTop(newY);
        return;
    }

    // For resizing, anchor the opposite corner/side and scale proportionally with aspect ratio R
    double minW = 0.1;
    double minH = minW / R;

    QRectF r = m_dragStartCropRect;

    switch (m_activeHandle) {
        case BottomRight: {
            // Anchor top-left (r.left(), r.top())
            double targetW = std::max(minW, r.width() + dx);
            double targetH = targetW / R;
            // Bound inside [0, 1]
            if (r.left() + targetW > 1.0) {
                targetW = 1.0 - r.left();
                targetH = targetW / R;
            }
            if (r.top() + targetH > 1.0) {
                targetH = 1.0 - r.top();
                targetW = targetH * R;
            }
            m_cropRect = QRectF(r.left(), r.top(), targetW, targetH);
            break;
        }
        case BottomLeft: {
            // Anchor top-right (r.right(), r.top())
            double targetW = std::max(minW, r.width() - dx);
            double targetH = targetW / R;
            if (r.right() - targetW < 0.0) {
                targetW = r.right();
                targetH = targetW / R;
            }
            if (r.top() + targetH > 1.0) {
                targetH = 1.0 - r.top();
                targetW = targetH * R;
            }
            m_cropRect = QRectF(r.right() - targetW, r.top(), targetW, targetH);
            break;
        }
        case TopRight: {
            // Anchor bottom-left (r.left(), r.bottom())
            double targetW = std::max(minW, r.width() + dx);
            double targetH = targetW / R;
            if (r.left() + targetW > 1.0) {
                targetW = 1.0 - r.left();
                targetH = targetW / R;
            }
            if (r.bottom() - targetH < 0.0) {
                targetH = r.bottom();
                targetW = targetH * R;
            }
            m_cropRect = QRectF(r.left(), r.bottom() - targetH, targetW, targetH);
            break;
        }
        case TopLeft: {
            // Anchor bottom-right (r.right(), r.bottom())
            double targetW = std::max(minW, r.width() - dx);
            double targetH = targetW / R;
            if (r.right() - targetW < 0.0) {
                targetW = r.right();
                targetH = targetW / R;
            }
            if (r.bottom() - targetH < 0.0) {
                targetH = r.bottom();
                targetW = targetH * R;
            }
            m_cropRect = QRectF(r.right() - targetW, r.bottom() - targetH, targetW, targetH);
            break;
        }
        case Right: {
            double targetW = std::max(minW, r.width() + dx);
            double targetH = targetW / R;
            // Center vertically relative to start rect center
            double centerY = r.center().y();
            double top = centerY - targetH * 0.5;
            double bottom = centerY + targetH * 0.5;
            if (top < 0.0) {
                top = 0.0;
                targetH = std::min(1.0, (bottom - top));
                targetW = targetH * R;
            }
            if (bottom > 1.0) {
                targetH = std::min(1.0, 1.0 - top);
                targetW = targetH * R;
            }
            if (r.left() + targetW > 1.0) {
                targetW = 1.0 - r.left();
                targetH = targetW / R;
            }
            m_cropRect = QRectF(r.left(), top, targetW, targetH);
            break;
        }
        case Left: {
            double targetW = std::max(minW, r.width() - dx);
            double targetH = targetW / R;
            double centerY = r.center().y();
            double top = centerY - targetH * 0.5;
            double bottom = centerY + targetH * 0.5;
            if (top < 0.0) {
                top = 0.0;
                targetH = std::min(1.0, (bottom - top));
                targetW = targetH * R;
            }
            if (bottom > 1.0) {
                targetH = std::min(1.0, 1.0 - top);
                targetW = targetH * R;
            }
            if (r.right() - targetW < 0.0) {
                targetW = r.right();
                targetH = targetW / R;
            }
            m_cropRect = QRectF(r.right() - targetW, top, targetW, targetH);
            break;
        }
        case Bottom: {
            double targetH = std::max(minH, r.height() + dy);
            double targetW = targetH * R;
            double centerX = r.center().x();
            double left = centerX - targetW * 0.5;
            double right = centerX + targetW * 0.5;
            if (left < 0.0) {
                left = 0.0;
                targetW = std::min(1.0, right - left);
                targetH = targetW / R;
            }
            if (right > 1.0) {
                targetW = std::min(1.0, 1.0 - left);
                targetH = targetW / R;
            }
            if (r.top() + targetH > 1.0) {
                targetH = 1.0 - r.top();
                targetW = targetH * R;
            }
            m_cropRect = QRectF(left, r.top(), targetW, targetH);
            break;
        }
        case Top: {
            double targetH = std::max(minH, r.height() - dy);
            double targetW = targetH * R;
            double centerX = r.center().x();
            double left = centerX - targetW * 0.5;
            double right = centerX + targetW * 0.5;
            if (left < 0.0) {
                left = 0.0;
                targetW = std::min(1.0, right - left);
                targetH = targetW / R;
            }
            if (right > 1.0) {
                targetW = std::min(1.0, 1.0 - left);
                targetH = targetW / R;
            }
            if (r.bottom() - targetH < 0.0) {
                targetH = r.bottom();
                targetW = targetH * R;
            }
            m_cropRect = QRectF(left, r.bottom() - targetH, targetW, targetH);
            break;
        }
        default:
            break;
    }
}
