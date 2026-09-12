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

bool V4L2Output::writeFrameRGBA(const uint8_t* __restrict__ rgba, int width, int height) {
    if (m_fd < 0 || width != m_width || height != m_height) return false;

    const size_t expectedSize = static_cast<size_t>(width * height * 2);
    if (m_yuyvBuffer.size() != expectedSize) {
        m_yuyvBuffer.resize(expectedSize);
    }

    uint8_t* __restrict__ out = m_yuyvBuffer.data();
    const int totalPixels = width * height;

    // Fast pointer-based RGBA -> YUYV conversion
    // Rec.601 integer fixed-point coefficients:
    // Y0 = (19595*R0 + 38469*G0 + 7472*B0) >> 16
    // Y1 = (19595*R1 + 38469*G1 + 7472*B1) >> 16
    // U  = ((-11059*R_avg - 21709*G_avg + 32768*B_avg) >> 16) + 128
    // V  = (( 32768*R_avg - 27439*G_avg -  5329*B_avg) >> 16) + 128
    const uint8_t* pSrc = rgba;
    uint8_t* pDst = out;
    const uint8_t* pSrcEnd = rgba + totalPixels * 4;

    while (pSrc < pSrcEnd) {
        const int r0 = pSrc[0];
        const int g0 = pSrc[1];
        const int b0 = pSrc[2];

        const int r1 = pSrc[4];
        const int g1 = pSrc[5];
        const int b1 = pSrc[6];
        pSrc += 8;

        const int y0 = (19595 * r0 + 38469 * g0 +  7472 * b0) >> 16;
        const int y1 = (19595 * r1 + 38469 * g1 +  7472 * b1) >> 16;

        const int rAvg = (r0 + r1) >> 1;
        const int gAvg = (g0 + g1) >> 1;
        const int bAvg = (b0 + b1) >> 1;

        const int u = ((-11059 * rAvg - 21709 * gAvg + 32768 * bAvg) >> 16) + 128;
        const int v = (( 32768 * rAvg - 27439 * gAvg -  5329 * bAvg) >> 16) + 128;

        pDst[0] = clamp8(y0);
        pDst[1] = clamp8(u);
        pDst[2] = clamp8(y1);
        pDst[3] = clamp8(v);
        pDst += 4;
    }

    ssize_t written = write(m_fd, out, expectedSize);
    return written == static_cast<ssize_t>(expectedSize);
}
