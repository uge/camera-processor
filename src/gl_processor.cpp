#include "gl_processor.h"
#include <iostream>

static const char* vertexShaderSource = R"(
attribute vec2 aPos;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* fragmentShaderSource = R"(
varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform sampler2D uLutTexture;
uniform int uUseLut;

uniform float uBrightness;
uniform float uContrast;
uniform float uGamma;
uniform float uSaturation;
uniform float uTemperature;
uniform float uSharpness;
uniform int uMirror;
uniform vec2 uTexelSize;
uniform vec4 uCropRect; // x, y, width, height in normalized [0, 1] coords

// RGB <-> HSV helper
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(c.r, p.yzx), vec4(p.x, c.r, p.z, K.x), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec2 uv = vTexCoord;
    if (uMirror == 1) {
        uv.x = 1.0 - uv.x;
    }

    // Apply crop rectangle mapping
    uv = uCropRect.xy + uv * uCropRect.zw;

    vec4 color = texture2D(uTexture, uv);
    vec3 rgb = color.rgb;

    // 1. Unsharp mask (convolution sharpening)
    if (uSharpness > 0.001) {
        vec3 n = texture2D(uTexture, uv + vec2(0.0, -uTexelSize.y)).rgb;
        vec3 s = texture2D(uTexture, uv + vec2(0.0, uTexelSize.y)).rgb;
        vec3 e = texture2D(uTexture, uv + vec2(uTexelSize.x, 0.0)).rgb;
        vec3 w = texture2D(uTexture, uv + vec2(-uTexelSize.x, 0.0)).rgb;
        vec3 blurred = (n + s + e + w) * 0.25;
        rgb = clamp(rgb + (rgb - blurred) * uSharpness, 0.0, 1.0);
    }

    // 2. Brightness
    rgb += uBrightness;

    // 3. Contrast around midpoint 0.5
    rgb = (rgb - 0.5) * uContrast + 0.5;

    // 4. Contrast Curve (LUT mapping)
    if (uUseLut == 1) {
        rgb = clamp(rgb, 0.0, 1.0);
        float rCurve = texture2D(uLutTexture, vec2(rgb.r, 0.5)).r;
        float gCurve = texture2D(uLutTexture, vec2(rgb.g, 0.5)).r;
        float bCurve = texture2D(uLutTexture, vec2(rgb.b, 0.5)).r;
        rgb = vec3(rCurve, gCurve, bCurve);
    }

    // 5. Color temperature (Warm / Cool balance)
    rgb.r += uTemperature * 0.12;
    rgb.b -= uTemperature * 0.12;

    // 6. Saturation in HSV space
    if (abs(uSaturation - 1.0) > 0.001) {
        vec3 hsv = rgb2hsv(clamp(rgb, 0.0, 1.0));
        hsv.y = clamp(hsv.y * uSaturation, 0.0, 1.0);
        rgb = hsv2rgb(hsv);
    }

    // 7. Gamma correction
    rgb = clamp(rgb, 0.0, 1.0);
    if (abs(uGamma - 1.0) > 0.001 && uGamma > 0.01) {
        rgb = pow(rgb, vec3(1.0 / uGamma));
    }

    gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

GLProcessor::GLProcessor(QWidget* parent)
    : QOpenGLWidget(parent) {
    m_lutData.resize(256);
    for (int i = 0; i < 256; ++i) m_lutData[i] = static_cast<uint8_t>(i);
    m_lutNeedsUpload = true;
}

GLProcessor::~GLProcessor() {
    makeCurrent();
    if (m_inputTex) glDeleteTextures(1, &m_inputTex);
    if (m_lutTex) glDeleteTextures(1, &m_lutTex);
    if (m_fboTex) glDeleteTextures(1, &m_fboTex);
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    doneCurrent();
}

void GLProcessor::setFilters(const ImageFilters& filters) {
    m_filters = filters;
    update();
}

void GLProcessor::setCurveLut(const std::vector<uint8_t>& lut) {
    if (lut.size() == 256) {
        m_lutData = lut;
        m_lutNeedsUpload = true;
        update();
    }
}

void GLProcessor::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    setupShaders();

    glGenTextures(1, &m_inputTex);
    glBindTexture(GL_TEXTURE_2D, m_inputTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_lutTex);
    glBindTexture(GL_TEXTURE_2D, m_lutTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 1, 0, GL_RED, GL_UNSIGNED_BYTE, m_lutData.data());
    m_lutNeedsUpload = false;
}

void GLProcessor::setupShaders() {
    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    m_program->link();

    m_uBrightness = m_program->uniformLocation("uBrightness");
    m_uContrast = m_program->uniformLocation("uContrast");
    m_uGamma = m_program->uniformLocation("uGamma");
    m_uSaturation = m_program->uniformLocation("uSaturation");
    m_uTemperature = m_program->uniformLocation("uTemperature");
    m_uSharpness = m_program->uniformLocation("uSharpness");
    m_uMirror = m_program->uniformLocation("uMirror");
    m_uTexelSize = m_program->uniformLocation("uTexelSize");
    m_uLutTexture = m_program->uniformLocation("uLutTexture");
    m_uUseLut = m_program->uniformLocation("uUseLut");
    m_uCropRect = m_program->uniformLocation("uCropRect");
}

void GLProcessor::setCropRect(const QRectF& cropRect) {
    m_cropRect = cropRect;
    update();
}

void GLProcessor::setShowProcessed(bool showProcessed) {
    m_showProcessed = showProcessed;
    update();
}

void GLProcessor::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void GLProcessor::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_program || !m_inputTex || m_inWidth == 0 || m_inHeight == 0) {
        return;
    }

    if (m_lutNeedsUpload && m_lutTex) {
        glBindTexture(GL_TEXTURE_2D, m_lutTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RED, GL_UNSIGNED_BYTE, m_lutData.data());
        m_lutNeedsUpload = false;
    }

    m_program->bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_inputTex);
    m_program->setUniformValue("uTexture", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_lutTex);
    m_program->setUniformValue(m_uLutTexture, 1);

    if (m_showProcessed) {
        // Output pane: apply all shader filters and crop zoom
        m_program->setUniformValue(m_uUseLut, 1);
        m_program->setUniformValue(m_uBrightness, m_filters.brightness);
        m_program->setUniformValue(m_uContrast, m_filters.contrast);
        m_program->setUniformValue(m_uGamma, m_filters.gamma);
        m_program->setUniformValue(m_uSaturation, m_filters.saturation);
        m_program->setUniformValue(m_uTemperature, m_filters.temperature);
        m_program->setUniformValue(m_uSharpness, m_filters.sharpness);
        m_program->setUniformValue(m_uMirror, m_filters.mirror ? 1 : 0);
        m_program->setUniformValue(m_uTexelSize, 1.0f / m_inWidth, 1.0f / m_inHeight);
        m_program->setUniformValue(m_uCropRect, (float)m_cropRect.x(), (float)m_cropRect.y(),
                                   (float)m_cropRect.width(), (float)m_cropRect.height());
    } else {
        // Input pane: pure raw camera image, full frame
        m_program->setUniformValue(m_uUseLut, 0);
        m_program->setUniformValue(m_uBrightness, 0.0f);
        m_program->setUniformValue(m_uContrast, 1.0f);
        m_program->setUniformValue(m_uGamma, 1.0f);
        m_program->setUniformValue(m_uSaturation, 1.0f);
        m_program->setUniformValue(m_uTemperature, 0.0f);
        m_program->setUniformValue(m_uSharpness, 0.0f);
        m_program->setUniformValue(m_uMirror, 0);
        m_program->setUniformValue(m_uTexelSize, 1.0f / m_inWidth, 1.0f / m_inHeight);
        m_program->setUniformValue(m_uCropRect, 0.0f, 0.0f, 1.0f, 1.0f);
    }

    // Calculate quad vertices maintaining camera aspect ratio (letterbox / pillarbox)
    float viewW = (float)width();
    float viewH = (float)height();
    float viewAspect = (viewH > 0.0f) ? (viewW / viewH) : 1.0f;
    float camAspect = (m_inHeight > 0) ? ((float)m_inWidth / (float)m_inHeight) : (16.0f / 9.0f);

    float quadW = 1.0f;
    float quadH = 1.0f;
    if (viewAspect > camAspect) {
        // Widget is wider than camera image -> pillarbox (scale X down)
        quadW = camAspect / viewAspect;
        quadH = 1.0f;
    } else {
        // Widget is taller than camera image -> letterbox (scale Y down)
        quadW = 1.0f;
        quadH = viewAspect / camAspect;
    }

    // Quad geometry (OpenGL texture coordinate origin is bottom-left, but camera image is top-down,
    // so we invert V coordinates 0 -> 1 to flip it upright for display)
    const GLfloat vertices[] = {
        -quadW, -quadH,  0.0f, 1.0f,
         quadW, -quadH,  1.0f, 1.0f,
        -quadW,  quadH,  0.0f, 0.0f,
         quadW,  quadH,  1.0f, 0.0f,
    };

    m_program->enableAttributeArray("aPos");
    m_program->setAttributeArray("aPos", GL_FLOAT, vertices, 2, 4 * sizeof(GLfloat));

    m_program->enableAttributeArray("aTexCoord");
    m_program->setAttributeArray("aTexCoord", GL_FLOAT, &vertices[2], 2, 4 * sizeof(GLfloat));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_program->disableAttributeArray("aPos");
    m_program->disableAttributeArray("aTexCoord");
    m_program->release();
}

void GLProcessor::processFrame(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& outProcessedRgba) {
    makeCurrent();

    if (m_inWidth != width || m_inHeight != height) {
        m_inWidth = width;
        m_inHeight = height;

        // Reallocate input texture
        glBindTexture(GL_TEXTURE_2D, m_inputTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        // Reallocate FBO for offscreen readback
        if (m_fbo) {
            glDeleteFramebuffers(1, &m_fbo);
            glDeleteTextures(1, &m_fboTex);
        }
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glGenTextures(1, &m_fboTex);
        glBindTexture(GL_TEXTURE_2D, m_fboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fboTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    }

    // Upload raw camera texture
    glBindTexture(GL_TEXTURE_2D, m_inputTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    // 1. Render to FBO for output device write
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, width, height);

    if (m_lutNeedsUpload && m_lutTex) {
        glBindTexture(GL_TEXTURE_2D, m_lutTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RED, GL_UNSIGNED_BYTE, m_lutData.data());
        m_lutNeedsUpload = false;
    }

    m_program->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_inputTex);
    m_program->setUniformValue("uTexture", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_lutTex);
    m_program->setUniformValue(m_uLutTexture, 1);
    m_program->setUniformValue(m_uUseLut, 1);

    m_program->setUniformValue(m_uBrightness, m_filters.brightness);
    m_program->setUniformValue(m_uContrast, m_filters.contrast);
    m_program->setUniformValue(m_uGamma, m_filters.gamma);
    m_program->setUniformValue(m_uSaturation, m_filters.saturation);
    m_program->setUniformValue(m_uTemperature, m_filters.temperature);
    m_program->setUniformValue(m_uSharpness, m_filters.sharpness);
    m_program->setUniformValue(m_uMirror, m_filters.mirror ? 1 : 0);
    m_program->setUniformValue(m_uTexelSize, 1.0f / width, 1.0f / height);
    m_program->setUniformValue(m_uCropRect, (float)m_cropRect.x(), (float)m_cropRect.y(),
                               (float)m_cropRect.width(), (float)m_cropRect.height());

    // For FBO write, since glReadPixels reads starting from the bottom scanline upwards,
    // we invert texture V coordinates (0.0 at top quad vertex, 1.0 at bottom quad vertex)
    // so the resulting readback buffer matches top-to-bottom scanlines for V4L2.
    static const GLfloat fboVertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };

    m_program->enableAttributeArray("aPos");
    m_program->setAttributeArray("aPos", GL_FLOAT, fboVertices, 2, 4 * sizeof(GLfloat));

    m_program->enableAttributeArray("aTexCoord");
    m_program->setAttributeArray("aTexCoord", GL_FLOAT, &fboVertices[2], 2, 4 * sizeof(GLfloat));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_program->disableAttributeArray("aPos");
    m_program->disableAttributeArray("aTexCoord");
    m_program->release();

    outProcessedRgba.resize(width * height * 4);
    // Read back pixels directly into memory buffer
    // Note: glReadPixels starts from bottom line up, which in standard coords would be inverted,
    // but combined with shader UV orientation matches standard V4L2 top-to-bottom scanlines.
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outProcessedRgba.data());

    // Restore to default framebuffer for preview widget
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

    // Trigger widget redraw
    update();
}

void GLProcessor::updateRawFrame(const uint8_t* rgba, int width, int height) {
    makeCurrent();

    if (m_inWidth != width || m_inHeight != height) {
        m_inWidth = width;
        m_inHeight = height;

        glBindTexture(GL_TEXTURE_2D, m_inputTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    glBindTexture(GL_TEXTURE_2D, m_inputTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    update();
}
