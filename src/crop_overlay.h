#pragma once

#include <QWidget>
#include <QRectF>
#include <QImage>

class CropOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CropOverlay(QWidget* parent = nullptr);

    // Set target output aspect ratio (e.g. 1280.0 / 720.0 = 16:9)
    void setTargetAspectRatio(double ratio);

    // Set source resolution aspect ratio
    void setSourceAspectRatio(double ratio);

    // Reset crop to maximum rectangle fitting within the source
    void resetCrop();

    // Normalized crop rect [0.0, 1.0] relative to input video frame
    QRectF getCropRect() const { return m_cropRect; }

    enum SampleMode {
        SampleNone = 0,
        SampleWhite,
        SampleBlack,
        SampleGrey
    };

    void setSampleMode(SampleMode mode);
    SampleMode getSampleMode() const { return m_sampleMode; }

signals:
    void cropChanged(const QRectF& rect);
    void samplePointClicked(SampleMode mode, const QPointF& normalizedPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum Handle {
        None = 0,
        Move,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Top,
        Bottom,
        Left,
        Right
    };

    QRectF getVideoRectInWidget() const;
    Handle hitTest(const QPoint& pos) const;
    QRectF normalizedToWidget(const QRectF& normRect) const;
    QRectF widgetToNormalized(const QRectF& widgetRect) const;
    void applyDrag(const QPoint& curPos);

    double m_targetAspect = 16.0 / 9.0;
    double m_sourceAspect = 16.0 / 9.0;

    // Crop rectangle in normalized [0.0, 1.0] coordinates relative to video frame
    QRectF m_cropRect{0.0, 0.0, 1.0, 1.0};

    SampleMode m_sampleMode = SampleNone;
    Handle m_activeHandle = None;
    QPoint m_dragStartPos;
    QRectF m_dragStartCropRect;
};
