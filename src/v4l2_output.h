#pragma once

#include <string>
#include <vector>
#include <cstdint>

class V4L2Output {
public:
    V4L2Output();
    ~V4L2Output();

    // Opens virtual loopback device (e.g. /dev/video10) and configures format (YUYV 4:2:2)
    bool openDevice(const std::string& devName, int width, int height);
    void closeDevice();

    // Converts RGBA to YUYV and writes frame to /dev/videoX
    bool writeFrameRGBA(const uint8_t* rgba, int width, int height);

    bool isOpen() const { return m_fd >= 0; }

private:
    int m_fd = -1;
    std::string m_devName;
    int m_width = 0;
    int m_height = 0;
    std::vector<uint8_t> m_yuyvBuffer;
};
