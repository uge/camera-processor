#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <linux/videodev2.h>
#include <turbojpeg.h>

struct CameraControlInfo {
    uint32_t id;
    std::string name;
    int32_t min;
    int32_t max;
    int32_t step;
    int32_t def;
    int32_t current;
};

class V4L2Capture {
public:
    V4L2Capture();
    ~V4L2Capture();

    bool openDevice(const std::string& devName, int width = 1280, int height = 720, int fps = 30);
    void closeDevice();

    bool startStreaming();
    void stopStreaming();

    // Grabs latest frame and decompresses to RGBA buffer.
    // Returns true if new frame decoded.
    bool grabFrameRGBA(std::vector<uint8_t>& outRgba, int& outWidth, int& outHeight);

    // V4L2 Hardware Controls
    std::vector<CameraControlInfo> getAvailableControls();
    bool setControl(uint32_t id, int32_t value);
    int32_t getControl(uint32_t id);

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

private:
    struct Buffer {
        void* start = nullptr;
        size_t length = 0;
    };

    int m_fd = -1;
    std::string m_devName;
    int m_width = 0;
    int m_height = 0;
    uint32_t m_pixelFormat = 0;
    std::vector<Buffer> m_buffers;
    bool m_streaming = false;

    tjhandle m_tjDecompressor = nullptr;
};
