#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
#include <vector>

#include "v4l2_capture.h"
#include "v4l2_output.h"
#include "gl_processor.h"
#include "curve_editor.h"
#include "crop_overlay.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onFrameTimer();
    void onShaderParamChanged();
    void onHwControlChanged(uint32_t id, int value);
    void onResetDefaults();
    void onColorSamplePointClicked(CropOverlay::SampleMode mode, const QPointF& normalizedPos);
    void applyScenePreset(const QString& sceneType);
    void onInstallLoopbackDevice();

private:
    void setupUI();
    bool tryOpenLoopback();
    void populateHwControls();
    QWidget* createSliderRow(const QString& labelText, int min, int max, int defaultVal,
                            std::function<void(int)> onChange, QLabel** outValLabel = nullptr);

    // Core subsystems
    V4L2Capture m_capture;
    V4L2Output m_output;
    GLProcessor* m_glInputProcessor = nullptr;
    GLProcessor* m_glProcessor = nullptr;
    CropOverlay* m_cropOverlay = nullptr;

    QTimer* m_frameTimer = nullptr;

    // Buffer cache
    std::vector<uint8_t> m_rawFrameRgba;
    std::vector<uint8_t> m_processedFrameRgba;
    int m_rawWidth = 0;
    int m_rawHeight = 0;

    // UI Sliders (Shader effects)
    QPushButton* m_btnSampleWhite = nullptr;
    QPushButton* m_btnSampleBlack = nullptr;
    QPushButton* m_btnSampleGrey = nullptr;
    QLabel* m_lblSampledColor = nullptr;

    QSlider* m_sliderBrightness = nullptr;
    QSlider* m_sliderContrast = nullptr;
    QSlider* m_sliderGamma = nullptr;
    QSlider* m_sliderSaturation = nullptr;
    QSlider* m_sliderTemperature = nullptr;
    QSlider* m_sliderSharpness = nullptr;
    QCheckBox* m_checkMirror = nullptr;
    CurveEditor* m_curveEditor = nullptr;

    // UI labels
    QLabel* m_lblFps = nullptr;
    QLabel* m_lblStatus = nullptr;
    QPushButton* m_btnInstallLoopback = nullptr;

    // Layout containers
    QVBoxLayout* m_hwControlsLayout = nullptr;
    std::map<uint32_t, QSlider*> m_hwSliders;
    std::map<uint32_t, CameraControlInfo> m_hwCtrlInfo;

    // Stats
    int m_frameCount = 0;
    int64_t m_lastFpsTime = 0;
};
