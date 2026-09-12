#pragma once

#include <QWidget>
#include <QPointF>
#include <vector>
#include <cstdint>

class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget* parent = nullptr);

    // Returns evaluated 256-entry 8-bit lookup table (LUT)
    std::vector<uint8_t> getLut() const;

    // Reset control points to default linear diagonal
    void resetLinear();

    // Preset S-curve for higher contrast
    void presetSCurve();

    // Preset High Dynamic Range / shadows lift
    void presetLiftShadows();

    // Set control point at index or adjust black/grey/white points
    void setBlackPoint(double inX, double outY = 0.0);
    void setWhitePoint(double inX, double outY = 1.0);
    void setGreyPoint(double inX, double outY = 0.5);

    const std::vector<QPointF>& getPoints() const { return m_points; }

signals:
    void curveChanged(const std::vector<uint8_t>& lut);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void rebuildLut();
    int findNearestPoint(const QPointF& normalizedPos, double threshold = 0.06) const;
    QPointF widgetToNormalized(const QPoint& p) const;
    QPoint normalizedToWidget(const QPointF& p) const;

    // Control points in normalized [0.0, 1.0] coordinates, sorted by x
    std::vector<QPointF> m_points;
    int m_draggedPointIdx = -1;

    // Computed 256-element LUT
    std::vector<uint8_t> m_lut;
};
