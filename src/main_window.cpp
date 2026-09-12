#include "main_window.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QDateTime>
#include <QEvent>
#include <QProcess>
#include <QMessageBox>
#include <iostream>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Camera Processor & V4L2 Virtual Device (/dev/video10)");
    resize(1400, 750);

    setupUI();

    // 1. Open Physical Camera (/dev/video0)
    if (!m_capture.openDevice("/dev/video0", 1280, 720, 30)) {
        m_lblStatus->setText("Status: <font color='red'>Failed to open /dev/video0</font>");
    } else {
        m_capture.startStreaming();
        populateHwControls();
        m_lblStatus->setText(QString("Input: /dev/video0 (%1x%2)")
                                 .arg(m_capture.getWidth())
                                 .arg(m_capture.getHeight()));
    }

    // 2. Open Loopback Output (/dev/video10)
    tryOpenLoopback();

    int outW = m_capture.getWidth() > 0 ? m_capture.getWidth() : 1280;
    int outH = m_capture.getHeight() > 0 ? m_capture.getHeight() : 720;
    if (m_cropOverlay && outH > 0 && m_capture.getHeight() > 0) {
        m_cropOverlay->setSourceAspectRatio((double)m_capture.getWidth() / m_capture.getHeight());
        m_cropOverlay->setTargetAspectRatio((double)outW / outH);
    }

    // High frequency frame timer (approx 60 Hz polling loop to feed 30 fps capture seamlessly)
    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &MainWindow::onFrameTimer);
    m_frameTimer->start(10); // 10ms intervals

    m_lastFpsTime = QDateTime::currentMSecsSinceEpoch();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_cropOverlay && m_glInputProcessor) {
        m_cropOverlay->setGeometry(0, 0, m_glInputProcessor->width(), m_glInputProcessor->height());
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_glInputProcessor && event->type() == QEvent::Resize) {
        if (m_cropOverlay) {
            m_cropOverlay->setGeometry(0, 0, m_glInputProcessor->width(), m_glInputProcessor->height());
            m_cropOverlay->raise();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

MainWindow::~MainWindow() {
    if (m_frameTimer) m_frameTimer->stop();
    m_capture.closeDevice();
    m_output.closeDevice();
}

QWidget* MainWindow::createSliderRow(const QString& labelText, int min, int max, int defaultVal,
                                     std::function<void(int)> onChange, QLabel** outValLabel) {
    QWidget* row = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);

    QLabel* lblName = new QLabel(labelText, row);
    lblName->setFixedWidth(130);

    QSlider* slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(min, max);
    slider->setValue(defaultVal);

    QLabel* lblVal = new QLabel(QString::number(defaultVal), row);
    lblVal->setFixedWidth(45);
    lblVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (outValLabel) *outValLabel = lblVal;

    connect(slider, &QSlider::valueChanged, [onChange, lblVal](int val) {
        lblVal->setText(QString::number(val));
        if (onChange) onChange(val);
    });

    layout->addWidget(lblName);
    layout->addWidget(slider, 1);
    layout->addWidget(lblVal);

    return row;
}

void MainWindow::setupUI() {
    QWidget* centralWidget = new QWidget(this);
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);

    // Left side: Dual OpenGL Previews (Input with Crop + Processed Output)
    QVBoxLayout* previewLayout = new QVBoxLayout();
    
    QHBoxLayout* dualPreviewLayout = new QHBoxLayout();
    dualPreviewLayout->setSpacing(8);

    // Pane 1: Input (Pre-processed) Camera View with Crop Overlay
    QGroupBox* grpInput = new QGroupBox("Input (Pre-processed Camera & Crop)", centralWidget);
    QVBoxLayout* inputLayout = new QVBoxLayout(grpInput);
    inputLayout->setContentsMargins(4, 4, 4, 4);

    m_glInputProcessor = new GLProcessor(grpInput);
    m_glInputProcessor->setShowProcessed(false);
    m_glInputProcessor->setMinimumSize(320, 240);
    m_glInputProcessor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    inputLayout->addWidget(m_glInputProcessor);

    m_cropOverlay = new CropOverlay(m_glInputProcessor);
    m_glInputProcessor->installEventFilter(this);

    connect(m_cropOverlay, &CropOverlay::cropChanged, [this](const QRectF& rect) {
        if (m_glProcessor) {
            m_glProcessor->setCropRect(rect);
        }
    });

    connect(m_cropOverlay, &CropOverlay::samplePointClicked, this, &MainWindow::onColorSamplePointClicked);

    dualPreviewLayout->addWidget(grpInput, 1);

    // Pane 2: Output (Post-processed & Cropped) View
    QGroupBox* grpOutput = new QGroupBox("Output (Post-processed -> /dev/video10)", centralWidget);
    QVBoxLayout* outputLayout = new QVBoxLayout(grpOutput);
    outputLayout->setContentsMargins(4, 4, 4, 4);

    m_glProcessor = new GLProcessor(grpOutput);
    m_glProcessor->setShowProcessed(true);
    m_glProcessor->setMinimumSize(320, 240);
    m_glProcessor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outputLayout->addWidget(m_glProcessor);
    dualPreviewLayout->addWidget(grpOutput, 1);

    previewLayout->addLayout(dualPreviewLayout, 1);

    // Status bar underneath preview
    QHBoxLayout* statusLayout = new QHBoxLayout();
    m_lblStatus = new QLabel("Initializing...", this);
    m_btnInstallLoopback = new QPushButton("Install /dev/video10", this);
    m_btnInstallLoopback->setVisible(false);
    m_btnInstallLoopback->setStyleSheet("background-color: #ff9900; color: #000; font-weight: bold; padding: 2px 8px;");
    connect(m_btnInstallLoopback, &QPushButton::clicked, this, &MainWindow::onInstallLoopbackDevice);

    m_lblFps = new QLabel("FPS: 0", this);
    m_lblFps->setFixedWidth(80);
    statusLayout->addWidget(m_lblStatus, 1);
    statusLayout->addWidget(m_btnInstallLoopback);
    statusLayout->addWidget(m_lblFps);
    previewLayout->addLayout(statusLayout);

    mainLayout->addLayout(previewLayout, 3);

    // Right side: Controls in scroll area
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFixedWidth(380);

    QWidget* controlContainer = new QWidget();
    QVBoxLayout* controlsLayout = new QVBoxLayout(controlContainer);

    // Group 1: Real-time GPU Shader Filters
    QGroupBox* grpShader = new QGroupBox("Real-Time Accelerated Processing", controlContainer);
    QVBoxLayout* shaderLayout = new QVBoxLayout(grpShader);

    // Brightness: -100 to +100 (maps to -1.0 .. 1.0)
    QWidget* rowBri = createSliderRow("Brightness", -100, 100, 0, [this](int) { onShaderParamChanged(); });
    m_sliderBrightness = rowBri->findChild<QSlider*>();
    shaderLayout->addWidget(rowBri);

    // Contrast: 0 to 300 (maps to 0.0 .. 3.0, neutral = 100)
    QWidget* rowCon = createSliderRow("Contrast", 0, 300, 100, [this](int) { onShaderParamChanged(); });
    m_sliderContrast = rowCon->findChild<QSlider*>();
    shaderLayout->addWidget(rowCon);

    // Gamma: 20 to 300 (maps to 0.2 .. 3.0, neutral = 100)
    QWidget* rowGam = createSliderRow("Gamma", 20, 300, 100, [this](int) { onShaderParamChanged(); });
    m_sliderGamma = rowGam->findChild<QSlider*>();
    shaderLayout->addWidget(rowGam);

    // Saturation: 0 to 300 (maps to 0.0 .. 3.0, neutral = 100)
    QWidget* rowSat = createSliderRow("Saturation", 0, 300, 100, [this](int) { onShaderParamChanged(); });
    m_sliderSaturation = rowSat->findChild<QSlider*>();
    shaderLayout->addWidget(rowSat);

    // Color Temperature: -100 to 100 (Cool to Warm)
    QWidget* rowTemp = createSliderRow("Color Temp", -100, 100, 0, [this](int) { onShaderParamChanged(); });
    m_sliderTemperature = rowTemp->findChild<QSlider*>();
    shaderLayout->addWidget(rowTemp);

    // Sharpness: 0 to 300 (0 to 3.0)
    QWidget* rowSharp = createSliderRow("Sharpness", 0, 300, 0, [this](int) { onShaderParamChanged(); });
    m_sliderSharpness = rowSharp->findChild<QSlider*>();
    shaderLayout->addWidget(rowSharp);

    // Temporal Noise Reduction (3-frame sliding window)
    m_checkTnr = new QCheckBox("Temporal Noise Reduction (3-Frame Window)", grpShader);
    connect(m_checkTnr, &QCheckBox::toggled, this, &MainWindow::onShaderParamChanged);
    shaderLayout->addWidget(m_checkTnr);

    QWidget* rowTnr = createSliderRow("TNR Strength", 10, 100, 50, [this](int) { onShaderParamChanged(); });
    m_sliderTnrStrength = rowTnr->findChild<QSlider*>();
    shaderLayout->addWidget(rowTnr);

    // Mirror flip
    m_checkMirror = new QCheckBox("Mirror / Flip Horizontal", grpShader);
    connect(m_checkMirror, &QCheckBox::toggled, this, &MainWindow::onShaderParamChanged);
    shaderLayout->addWidget(m_checkMirror);

    // Contrast Tone Curve Editor
    QLabel* lblCurveTitle = new QLabel("<b>Contrast Tone Curve</b> (Click to add, drag, dbl-click reset)", grpShader);
    lblCurveTitle->setWordWrap(true);
    shaderLayout->addWidget(lblCurveTitle);

    m_curveEditor = new CurveEditor(grpShader);
    shaderLayout->addWidget(m_curveEditor);

    connect(m_curveEditor, &CurveEditor::curveChanged, [this](const std::vector<uint8_t>& lut) {
        if (m_glProcessor) {
            m_glProcessor->setCurveLut(lut);
        }
    });

    // Preset buttons
    QHBoxLayout* presetLayout = new QHBoxLayout();
    QPushButton* btnLinear = new QPushButton("Linear", grpShader);
    QPushButton* btnSCurve = new QPushButton("S-Curve", grpShader);
    QPushButton* btnLift = new QPushButton("Lift Shadows", grpShader);
    presetLayout->addWidget(btnLinear);
    presetLayout->addWidget(btnSCurve);
    presetLayout->addWidget(btnLift);
    shaderLayout->addLayout(presetLayout);

    connect(btnLinear, &QPushButton::clicked, m_curveEditor, &CurveEditor::resetLinear);
    connect(btnSCurve, &QPushButton::clicked, m_curveEditor, &CurveEditor::presetSCurve);
    connect(btnLift, &QPushButton::clicked, m_curveEditor, &CurveEditor::presetLiftShadows);

    // Color Sampling UI (White point, Black point, Grey point)
    QGroupBox* grpSampling = new QGroupBox("Image Color Sampling", grpShader);
    QVBoxLayout* sampleLayout = new QVBoxLayout(grpSampling);
    sampleLayout->setContentsMargins(6, 6, 6, 6);

    QLabel* lblSampleDesc = new QLabel("Click a button below, then click a pixel on the input camera image to calibrate:", grpSampling);
    lblSampleDesc->setWordWrap(true);
    sampleLayout->addWidget(lblSampleDesc);

    QHBoxLayout* sampleBtnLayout = new QHBoxLayout();
    m_btnSampleBlack = new QPushButton("Sample Black", grpSampling);
    m_btnSampleGrey = new QPushButton("Sample Grey", grpSampling);
    m_btnSampleWhite = new QPushButton("Sample White", grpSampling);

    sampleBtnLayout->addWidget(m_btnSampleBlack);
    sampleBtnLayout->addWidget(m_btnSampleGrey);
    sampleBtnLayout->addWidget(m_btnSampleWhite);
    sampleLayout->addLayout(sampleBtnLayout);

    m_lblSampledColor = new QLabel("Last Sample: None", grpSampling);
    m_lblSampledColor->setStyleSheet("color: #aaa; font-size: 11px;");
    sampleLayout->addWidget(m_lblSampledColor);

    connect(m_btnSampleBlack, &QPushButton::clicked, [this]() {
        if (m_cropOverlay) {
            m_cropOverlay->setSampleMode(CropOverlay::SampleBlack);
            m_lblSampledColor->setText("Mode: Click a shadow / black point on input image...");
        }
    });
    connect(m_btnSampleGrey, &QPushButton::clicked, [this]() {
        if (m_cropOverlay) {
            m_cropOverlay->setSampleMode(CropOverlay::SampleGrey);
            m_lblSampledColor->setText("Mode: Click a neutral grey point on input image...");
        }
    });
    connect(m_btnSampleWhite, &QPushButton::clicked, [this]() {
        if (m_cropOverlay) {
            m_cropOverlay->setSampleMode(CropOverlay::SampleWhite);
            m_lblSampledColor->setText("Mode: Click a highlight / white point on input image...");
        }
    });

    shaderLayout->addWidget(grpSampling);

    QPushButton* btnResetCrop = new QPushButton("Reset Crop (Full Frame)", grpShader);
    connect(btnResetCrop, &QPushButton::clicked, [this]() {
        if (m_cropOverlay) m_cropOverlay->resetCrop();
    });
    shaderLayout->addWidget(btnResetCrop);

    QPushButton* btnReset = new QPushButton("Reset All Filters, Curve & Crop", grpShader);
    connect(btnReset, &QPushButton::clicked, this, &MainWindow::onResetDefaults);
    shaderLayout->addWidget(btnReset);

    controlsLayout->addWidget(grpShader);

    // Group 2: V4L2 Hardware Controls (Hardware sensor level)
    QGroupBox* grpHw = new QGroupBox("Hardware Sensor Controls (/dev/video0)", controlContainer);
    m_hwControlsLayout = new QVBoxLayout(grpHw);

    // Auto Scene Adjustment buttons for Input Device
    QGroupBox* grpSceneAuto = new QGroupBox("Auto Input Device Adjustment (Scene)", grpHw);
    QVBoxLayout* sceneAutoLayout = new QVBoxLayout(grpSceneAuto);
    sceneAutoLayout->setContentsMargins(6, 6, 6, 6);

    QLabel* lblSceneDesc = new QLabel("Automatically optimize hardware sensor exposure, gain, brightness, and contrast for scene conditions:", grpSceneAuto);
    lblSceneDesc->setWordWrap(true);
    sceneAutoLayout->addWidget(lblSceneDesc);

    QHBoxLayout* sceneBtnLayout = new QHBoxLayout();
    QPushButton* btnSceneDark = new QPushButton("Dark Scene", grpSceneAuto);
    QPushButton* btnSceneNeutral = new QPushButton("Neutral Scene", grpSceneAuto);
    QPushButton* btnSceneBright = new QPushButton("Bright Scene", grpSceneAuto);

    btnSceneDark->setToolTip("Boost sensor gain & brightness, disable backlight compensation, soften contrast for dim lighting");
    btnSceneNeutral->setToolTip("Restore factory default sensor calibration and balanced exposure");
    btnSceneBright->setToolTip("Lower gain & brightness, enable backlight compensation, increase contrast to handle glare / high lighting");

    sceneBtnLayout->addWidget(btnSceneDark);
    sceneBtnLayout->addWidget(btnSceneNeutral);
    sceneBtnLayout->addWidget(btnSceneBright);
    sceneAutoLayout->addLayout(sceneBtnLayout);

    connect(btnSceneDark, &QPushButton::clicked, [this]() { applyScenePreset("dark"); });
    connect(btnSceneNeutral, &QPushButton::clicked, [this]() { applyScenePreset("neutral"); });
    connect(btnSceneBright, &QPushButton::clicked, [this]() { applyScenePreset("bright"); });

    m_hwControlsLayout->addWidget(grpSceneAuto);
    controlsLayout->addWidget(grpHw);

    controlsLayout->addStretch(1);
    scrollArea->setWidget(controlContainer);
    mainLayout->addWidget(scrollArea, 1);

    setCentralWidget(centralWidget);
}

void MainWindow::populateHwControls() {
    auto ctrls = m_capture.getAvailableControls();
    m_hwSliders.clear();
    m_hwCtrlInfo.clear();

    for (const auto& c : ctrls) {
        uint32_t cid = c.id;
        m_hwCtrlInfo[cid] = c;
        QWidget* row = createSliderRow(
            QString::fromStdString(c.name),
            c.min, c.max, c.current,
            [this, cid](int val) {
                onHwControlChanged(cid, val);
            }
        );
        QSlider* slider = row->findChild<QSlider*>();
        if (slider) {
            m_hwSliders[cid] = slider;
        }
        m_hwControlsLayout->addWidget(row);
    }
}

void MainWindow::onHwControlChanged(uint32_t id, int value) {
    m_capture.setControl(id, value);
}

void MainWindow::onShaderParamChanged() {
    if (!m_sliderBrightness || !m_sliderContrast || !m_sliderGamma ||
        !m_sliderSaturation || !m_sliderTemperature || !m_sliderSharpness ||
        !m_checkMirror || !m_glProcessor) {
        return;
    }

    ImageFilters f;
    f.brightness = m_sliderBrightness->value() / 100.0f;
    f.contrast = m_sliderContrast->value() / 100.0f;
    f.gamma = m_sliderGamma->value() / 100.0f;
    f.saturation = m_sliderSaturation->value() / 100.0f;
    f.temperature = m_sliderTemperature->value() / 100.0f;
    f.sharpness = m_sliderSharpness->value() / 100.0f;
    f.mirror = m_checkMirror->isChecked();
    if (m_checkTnr) {
        f.tnrEnabled = m_checkTnr->isChecked();
    }
    if (m_sliderTnrStrength) {
        f.tnrStrength = m_sliderTnrStrength->value() / 100.0f;
    }

    m_glProcessor->setFilters(f);
}

void MainWindow::onResetDefaults() {
    m_sliderBrightness->setValue(0);
    m_sliderContrast->setValue(100);
    m_sliderGamma->setValue(100);
    m_sliderSaturation->setValue(100);
    m_sliderTemperature->setValue(0);
    m_sliderSharpness->setValue(0);
    if (m_checkTnr) m_checkTnr->setChecked(false);
    if (m_sliderTnrStrength) m_sliderTnrStrength->setValue(50);
    m_checkMirror->setChecked(false);
    if (m_curveEditor) {
        m_curveEditor->resetLinear();
    }
    if (m_cropOverlay) {
        m_cropOverlay->resetCrop();
    }
    onShaderParamChanged();
}

void MainWindow::onFrameTimer() {
    int w = 0, h = 0;
    if (m_capture.grabFrameRGBA(m_rawFrameRgba, w, h)) {
        m_rawWidth = w;
        m_rawHeight = h;
        // Update input pre-processed preview
        if (m_glInputProcessor) {
            m_glInputProcessor->updateRawFrame(m_rawFrameRgba.data(), w, h);
        }

        // Run GPU accelerated shader filters and retrieve processed buffer for output preview & virtual device
        m_glProcessor->processFrame(m_rawFrameRgba.data(), w, h, m_processedFrameRgba);

        // Feed to V4L2 virtual device (/dev/video10)
        if (m_output.isOpen()) {
            m_output.writeFrameRGBA(m_processedFrameRgba.data(), w, h);
        }

        m_frameCount++;
        int64_t now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastFpsTime >= 1000) {
            double fps = (m_frameCount * 1000.0) / (now - m_lastFpsTime);
            m_lblFps->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
            m_frameCount = 0;
            m_lastFpsTime = now;
        }
    }
}

void MainWindow::onColorSamplePointClicked(CropOverlay::SampleMode mode, const QPointF& normalizedPos) {
    if (m_rawFrameRgba.empty() || m_rawWidth <= 0 || m_rawHeight <= 0) return;

    // Convert normalized [0, 1] coords to pixel coords in raw image
    int px = std::clamp(static_cast<int>(normalizedPos.x() * m_rawWidth), 0, m_rawWidth - 1);
    int py = std::clamp(static_cast<int>(normalizedPos.y() * m_rawHeight), 0, m_rawHeight - 1);

    // 3x3 pixel neighborhood sampling for stability against noise
    int sumR = 0, sumG = 0, sumB = 0, count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        int y = std::clamp(py + dy, 0, m_rawHeight - 1);
        for (int dx = -1; dx <= 1; ++dx) {
            int x = std::clamp(px + dx, 0, m_rawWidth - 1);
            int idx = (y * m_rawWidth + x) * 4;
            sumR += m_rawFrameRgba[idx + 0];
            sumG += m_rawFrameRgba[idx + 1];
            sumB += m_rawFrameRgba[idx + 2];
            count++;
        }
    }

    int avgR = sumR / count;
    int avgG = sumG / count;
    int avgB = sumB / count;

    // Relative luminance / input X coordinate [0.0, 1.0]
    double inLuma = (0.299 * avgR + 0.587 * avgG + 0.114 * avgB) / 255.0;

    QString modeName;
    if (mode == CropOverlay::SampleWhite) {
        modeName = "White Point";
        if (m_curveEditor) {
            m_curveEditor->setWhitePoint(inLuma, 1.0);
        }
        // White balance adjustment using temperature slider
        // If sampled white is reddish (avgR > avgB), push cooler; if bluish (avgB > avgR), push warmer
        if (m_sliderTemperature) {
            double tempOffset = (avgB - avgR) / 255.0 * 200.0;
            int newTemp = std::clamp(static_cast<int>(m_sliderTemperature->value() + tempOffset), -100, 100);
            m_sliderTemperature->setValue(newTemp);
        }
    } else if (mode == CropOverlay::SampleBlack) {
        modeName = "Black Point";
        if (m_curveEditor) {
            m_curveEditor->setBlackPoint(inLuma, 0.0);
        }
    } else if (mode == CropOverlay::SampleGrey) {
        modeName = "Grey Point";
        if (m_curveEditor) {
            m_curveEditor->setGreyPoint(inLuma, 0.5);
        }
        // Grey balance tint adjustment
        if (m_sliderTemperature) {
            double tempOffset = (avgB - avgR) / 255.0 * 150.0;
            int newTemp = std::clamp(static_cast<int>(m_sliderTemperature->value() + tempOffset), -100, 100);
            m_sliderTemperature->setValue(newTemp);
        }
    }

    if (m_lblSampledColor) {
        QString hexColor = QString("#%1%2%3")
            .arg(avgR, 2, 16, QLatin1Char('0'))
            .arg(avgG, 2, 16, QLatin1Char('0'))
            .arg(avgB, 2, 16, QLatin1Char('0'));
        m_lblSampledColor->setText(QString("Sampled %1: RGB(%2, %3, %4) | Luma: %5")
            .arg(modeName)
            .arg(avgR).arg(avgG).arg(avgB)
            .arg(inLuma, 0, 'f', 2));
        m_lblSampledColor->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 11px;").arg(hexColor));
    }
}

void MainWindow::applyScenePreset(const QString& sceneType) {
    auto setHwVal = [this](uint32_t cid, int targetVal) {
        if (m_hwCtrlInfo.find(cid) != m_hwCtrlInfo.end()) {
            const auto& info = m_hwCtrlInfo[cid];
            int clamped = std::clamp(targetVal, info.min, info.max);
            m_capture.setControl(cid, clamped);
            if (m_hwSliders.find(cid) != m_hwSliders.end() && m_hwSliders[cid]) {
                m_hwSliders[cid]->blockSignals(true);
                m_hwSliders[cid]->setValue(clamped);
                m_hwSliders[cid]->blockSignals(false);
                // Also update the value label child in parent row
                QLabel* valLabel = m_hwSliders[cid]->parentWidget()->findChild<QLabel*>();
                // The row has lblName and lblVal; find child whose text is numeric
                const auto labels = m_hwSliders[cid]->parentWidget()->findChildren<QLabel*>();
                for (auto* lbl : labels) {
                    if (lbl != labels.front()) {
                        lbl->setText(QString::number(clamped));
                    }
                }
            }
        }
    };

    if (sceneType == "dark") {
        // In dim/dark environments:
        // - Increase sensor gain to amplify light
        // - Lift hardware brightness
        // - Slightly lower hardware contrast to avoid crushing shadow details
        // - Turn off backlight compensation to prevent darkening ambient areas
        // - Keep auto-white-balance enabled
        setHwVal(V4L2_CID_BRIGHTNESS, 165);
        setHwVal(V4L2_CID_CONTRAST, 120);
        setHwVal(V4L2_CID_GAIN, 220);
        setHwVal(V4L2_CID_BACKLIGHT_COMPENSATION, 0);
        setHwVal(V4L2_CID_AUTO_WHITE_BALANCE, 1);
        m_lblStatus->setText("Scene adjusted: <font color='#ffaa33'><b>Dark Scene</b> (High Gain, Lifted Exposure)</font>");
    } else if (sceneType == "bright") {
        // In bright/glare environments:
        // - Lower sensor gain to minimize sensor noise and prevent blowout
        // - Lower brightness to preserve highlights
        // - Moderately increase contrast for crisper edge separation
        // - Enable backlight compensation to balance intense background lighting
        // - Keep auto-white-balance enabled
        setHwVal(V4L2_CID_BRIGHTNESS, 105);
        setHwVal(V4L2_CID_CONTRAST, 155);
        setHwVal(V4L2_CID_GAIN, 80);
        setHwVal(V4L2_CID_BACKLIGHT_COMPENSATION, 1);
        setHwVal(V4L2_CID_AUTO_WHITE_BALANCE, 1);
        m_lblStatus->setText("Scene adjusted: <font color='#44ccff'><b>Bright Scene</b> (Low Gain, Antiglare, Backlight Comp)</font>");
    } else { // "neutral"
        // Restore hardware default values
        for (const auto& [cid, info] : m_hwCtrlInfo) {
            setHwVal(cid, info.def);
        }
        m_lblStatus->setText("Scene adjusted: <b>Neutral Scene</b> (Factory Defaults Restored)");
    }
}

bool MainWindow::tryOpenLoopback() {
    int outW = m_capture.getWidth() > 0 ? m_capture.getWidth() : 1280;
    int outH = m_capture.getHeight() > 0 ? m_capture.getHeight() : 720;

    if (m_output.openDevice("/dev/video10", outW, outH)) {
        m_lblStatus->setText(m_lblStatus->text() + " -> Output: /dev/video10 (Active)");
        if (m_btnInstallLoopback) m_btnInstallLoopback->setVisible(false);
        return true;
    } else {
        m_lblStatus->setText(m_lblStatus->text() + " -> <font color='orange'>/dev/video10 unavailable</font>");
        if (m_btnInstallLoopback) m_btnInstallLoopback->setVisible(true);
        return false;
    }
}

void MainWindow::onInstallLoopbackDevice() {
    // Prompt the user before requesting root privilege
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Install V4L2 Loopback Device",
        "The loopback virtual camera device (/dev/video10) is not loaded.\n\n"
        "Would you like to install and load it now using modprobe?\n"
        "(A privilege escalation prompt will ask for authentication)",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply != QMessageBox::Yes) {
        return;
    }

    m_lblStatus->setText("Loading v4l2loopback module via pkexec...");

    // Execute modprobe with pkexec for graphical polkit prompt
    QStringList args;
    args << "modprobe" << "v4l2loopback"
         << "video_nr=10"
         << "card_label=CameraProcessor"
         << "exclusive_caps=1";

    int exitCode = QProcess::execute("pkexec", args);

    if (exitCode == 0) {
        QMessageBox::information(this, "Success", "v4l2loopback module loaded successfully as /dev/video10!");
        // Re-attempt opening the output device
        if (tryOpenLoopback()) {
            m_lblStatus->setText(QString("Input: /dev/video0 (%1x%2) -> Output: /dev/video10 (Active)")
                                 .arg(m_capture.getWidth())
                                 .arg(m_capture.getHeight()));
        }
    } else {
        QMessageBox::warning(
            this,
            "Installation Failed",
            QString("Failed to load v4l2loopback (exit code %1).\n\n"
                    "You can manually install/load it from a terminal using:\n"
                    "  sudo modprobe v4l2loopback video_nr=10 card_label=\"CameraProcessor\" exclusive_caps=1")
                .arg(exitCode)
        );
        tryOpenLoopback();
    }
}
