#include "v4l2_output.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <cstring>
#include <iostream>

static int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

V4L2Output::V4L2Output() {}

V4L2Output::~V4L2Output() {
    closeDevice();
}

bool V4L2Output::openDevice(const std::string& devName, int width, int height) {
    closeDevice();
    m_devName = devName;
    m_width = width;
    m_height = height;

    m_fd = open(devName.c_str(), O_RDWR, 0);
    if (m_fd < 0) {
        std::cerr << "Failed to open loopback device " << devName << ": " << strerror(errno) << std::endl;
        return false;
    }

    v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.sizeimage = width * height * 2;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = width * 2;
    fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "VIDIOC_S_FMT failed on output device " << devName << ": " << strerror(errno) << std::endl;
        closeDevice();
        return false;
    }

    m_yuyvBuffer.resize(width * height * 2);
    return true;
}

void V4L2Output::closeDevice() {
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

static inline uint8_t clamp8(int val) {
    return static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
}

bool V4L2Output::writeFrameRGBA(const uint8_t* rgba, int width, int height) {
    if (m_fd < 0 || width != m_width || height != m_height) return false;

    size_t expectedSize = static_cast<size_t>(width * height * 2);
    if (m_yuyvBuffer.size() != expectedSize) {
        m_yuyvBuffer.resize(expectedSize);
    }

    uint8_t* out = m_yuyvBuffer.data();
    int totalPixels = width * height;
    int srcIdx = 0;
    int dstIdx = 0;

    // Convert RGBA -> YUYV
    // Rec.601 standard matrix:
    // Y  =  0.299 R + 0.587 G + 0.114 B
    // Cb = -0.168736 R - 0.331264 G + 0.5 B + 128
    // Cr =  0.5 R - 0.418688 G - 0.081312 B + 128
    for (int i = 0; i < totalPixels; i += 2) {
        int r0 = rgba[srcIdx + 0];
        int g0 = rgba[srcIdx + 1];
        int b0 = rgba[srcIdx + 2];

        int r1 = rgba[srcIdx + 4];
        int g1 = rgba[srcIdx + 5];
        int b1 = rgba[srcIdx + 6];
        srcIdx += 8;

        int y0 = ( 19595 * r0 + 38469 * g0 +  7472 * b0) >> 16;
        int y1 = ( 19595 * r1 + 38469 * g1 +  7472 * b1) >> 16;

        int rAvg = (r0 + r1) >> 1;
        int gAvg = (g0 + g1) >> 1;
        int bAvg = (b0 + b1) >> 1;

        int u = ((-11059 * rAvg - 21709 * gAvg + 32768 * bAvg) >> 16) + 128;
        int v = (( 32768 * rAvg - 27439 * gAvg -  5329 * bAvg) >> 16) + 128;

        out[dstIdx + 0] = clamp8(y0);
        out[dstIdx + 1] = clamp8(u);
        out[dstIdx + 2] = clamp8(y1);
        out[dstIdx + 3] = clamp8(v);
        dstIdx += 4;
    }

    ssize_t written = write(m_fd, out, expectedSize);
    return written == static_cast<ssize_t>(expectedSize);
}
