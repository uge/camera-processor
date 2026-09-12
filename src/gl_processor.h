#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <vector>
#include <cstdint>
#include <memory>

struct ImageFilters {
    float brightness = 0.0f;  // [-1.0, 1.0], 0 = neutral
    float contrast = 1.0f;    // [0.0, 3.0], 1 = neutral
    float gamma = 1.0f;       // [0.2, 3.0], 1 = neutral
    float saturation = 1.0f;  // [0.0, 3.0], 1 = neutral
    float temperature = 0.0f; // [-1.0, 1.0], warm vs cool
    float sharpness = 0.0f;   // [0.0, 3.0], 0 = off
    bool mirror = false;      // Horizontal flip
};

class GLProcessor : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit GLProcessor(QWidget* parent = nullptr);
    ~GLProcessor() override;

    void setFilters(const ImageFilters& filters);
    ImageFilters getFilters() const { return m_filters; }

    void setCurveLut(const std::vector<uint8_t>& lut);
    void setCropRect(const QRectF& cropRect);
    void setShowProcessed(bool showProcessed);

    // Upload raw RGBA frame from capture thread, process with GLSL shaders
    // and read back processed pixels for writing to loopback device.
    void processFrame(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& outProcessedRgba);

    // Update texture from raw RGBA frame without running FBO processing (for display-only widgets)
    void updateRawFrame(const uint8_t* rgba, int width, int height);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void setupShaders();
    void renderOffscreen(int width, int height);

    ImageFilters m_filters;
    std::unique_ptr<QOpenGLShaderProgram> m_program;

    GLuint m_inputTex = 0;
    GLuint m_lutTex = 0;
    GLuint m_fbo = 0;
    GLuint m_fboTex = 0;
    int m_fboWidth = 0;
    int m_fboHeight = 0;

    int m_inWidth = 0;
    int m_inHeight = 0;
    std::vector<uint8_t> m_latestRgba;
    std::vector<uint8_t> m_lutData;
    bool m_lutNeedsUpload = false;
    bool m_hasNewFrame = false;

    // Uniform locations
    int m_uBrightness = -1;
    int m_uContrast = -1;
    int m_uGamma = -1;
    int m_uSaturation = -1;
    int m_uTemperature = -1;
    int m_uSharpness = -1;
    int m_uMirror = -1;
    int m_uTexelSize = -1;
    int m_uLutTexture = -1;
    int m_uUseLut = -1;
    int m_uCropRect = -1;

    QRectF m_cropRect{0.0, 0.0, 1.0, 1.0};
    bool m_showProcessed = true;
};
