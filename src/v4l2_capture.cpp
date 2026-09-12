#include "v4l2_capture.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <iostream>

static int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

V4L2Capture::V4L2Capture() {
    m_tjDecompressor = tjInitDecompress();
}

V4L2Capture::~V4L2Capture() {
    closeDevice();
    if (m_tjDecompressor) {
        tjDestroy(m_tjDecompressor);
        m_tjDecompressor = nullptr;
    }
}

bool V4L2Capture::openDevice(const std::string& devName, int width, int height, int fps) {
    closeDevice();
    m_devName = devName;

    m_fd = open(devName.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (m_fd < 0) {
        std::cerr << "Failed to open camera device " << devName << ": " << strerror(errno) << std::endl;
        return false;
    }

    v4l2_capability cap;
    if (xioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "VIDIOC_QUERYCAP failed on " << devName << std::endl;
        closeDevice();
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::cerr << devName << " is not a video capture device" << std::endl;
        closeDevice();
        return false;
    }

    // Try MJPEG first for HD/FullHD efficiency, fallback to YUYV
    v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
        // Fallback to YUYV
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "VIDIOC_S_FMT failed" << std::endl;
            closeDevice();
            return false;
        }
    }

    m_width = fmt.fmt.pix.width;
    m_height = fmt.fmt.pix.height;
    m_pixelFormat = fmt.fmt.pix.pixelformat;

    // Set framerate
    v4l2_streamparm streamparm;
    std::memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.timeperframe.numerator = 1;
    streamparm.parm.capture.timeperframe.denominator = fps;
    xioctl(m_fd, VIDIOC_S_PARM, &streamparm);

    // Request buffers
    v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        std::cerr << "Insufficient buffer memory on " << devName << std::endl;
        closeDevice();
        return false;
    }

    m_buffers.resize(req.count);
    for (size_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "VIDIOC_QUERYBUF failed" << std::endl;
            closeDevice();
            return false;
        }

        m_buffers[i].length = buf.length;
        m_buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
        if (m_buffers[i].start == MAP_FAILED) {
            std::cerr << "mmap failed" << std::endl;
            closeDevice();
            return false;
        }
    }

    return true;
}

void V4L2Capture::closeDevice() {
    stopStreaming();
    for (auto& buf : m_buffers) {
        if (buf.start && buf.start != MAP_FAILED) {
            munmap(buf.start, buf.length);
        }
    }
    m_buffers.clear();

    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

bool V4L2Capture::startStreaming() {
    if (m_streaming || m_fd < 0) return m_streaming;

    for (size_t i = 0; i < m_buffers.size(); ++i) {
        v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "VIDIOC_QBUF failed on start" << std::endl;
            return false;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "VIDIOC_STREAMON failed" << std::endl;
        return false;
    }

    m_streaming = true;
    return true;
}

void V4L2Capture::stopStreaming() {
    if (!m_streaming || m_fd < 0) return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(m_fd, VIDIOC_STREAMOFF, &type);
    m_streaming = false;
}

static inline uint8_t clamp8(int val) {
    return static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
}

// Convert YUYV (YUV422 packed) to RGBA
static void yuyvToRgba(const uint8_t* yuyv, uint8_t* rgba, int width, int height) {
    int totalPixels = width * height;
    int srcIdx = 0;
    int dstIdx = 0;

    for (int i = 0; i < totalPixels; i += 2) {
        int y0 = yuyv[srcIdx + 0];
        int u  = yuyv[srcIdx + 1] - 128;
        int y1 = yuyv[srcIdx + 2];
        int v  = yuyv[srcIdx + 3] - 128;
        srcIdx += 4;

        // Pixel 0
        int r0 = y0 + (1402 * v) / 1000;
        int g0 = y0 - (344 * u + 714 * v) / 1000;
        int b0 = y0 + (1772 * u) / 1000;

        rgba[dstIdx + 0] = clamp8(r0);
        rgba[dstIdx + 1] = clamp8(g0);
        rgba[dstIdx + 2] = clamp8(b0);
        rgba[dstIdx + 3] = 255;

        // Pixel 1
        int r1 = y1 + (1402 * v) / 1000;
        int g1 = y1 - (344 * u + 714 * v) / 1000;
        int b1 = y1 + (1772 * u) / 1000;

        rgba[dstIdx + 4] = clamp8(r1);
        rgba[dstIdx + 5] = clamp8(g1);
        rgba[dstIdx + 6] = clamp8(b1);
        rgba[dstIdx + 7] = 255;

        dstIdx += 8;
    }
}

bool V4L2Capture::grabFrameRGBA(std::vector<uint8_t>& outRgba, int& outWidth, int& outHeight) {
    if (!m_streaming || m_fd < 0) return false;

    // To minimize latency, drain any backlog in the V4L2 queue and only keep the newest frame.
    // Older ready buffers are immediately re-queued to the kernel.
    v4l2_buffer latestBuf;
    std::memset(&latestBuf, 0, sizeof(latestBuf));
    latestBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    latestBuf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(m_fd, VIDIOC_DQBUF, &latestBuf) < 0) {
        if (errno == EAGAIN) return false;
        return false;
    }

    // Now check if there are even newer buffers waiting in the queue
    while (true) {
        v4l2_buffer nextBuf;
        std::memset(&nextBuf, 0, sizeof(nextBuf));
        nextBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        nextBuf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(m_fd, VIDIOC_DQBUF, &nextBuf) == 0) {
            // Re-queue the previous older buffer immediately
            xioctl(m_fd, VIDIOC_QBUF, &latestBuf);
            latestBuf = nextBuf;
        } else {
            break;
        }
    }

    outWidth = m_width;
    outHeight = m_height;
    outRgba.resize(m_width * m_height * 4);

    const uint8_t* rawData = static_cast<const uint8_t*>(m_buffers[latestBuf.index].start);
    size_t bytesUsed = latestBuf.bytesused;

    bool decoded = false;
    if (m_pixelFormat == V4L2_PIX_FMT_MJPEG && m_tjDecompressor) {
        int jpegSubsamp = 0;
        int w = 0, h = 0;
        if (tjDecompressHeader2(m_tjDecompressor, const_cast<unsigned char*>(rawData), bytesUsed, &w, &h, &jpegSubsamp) == 0) {
            if (w == m_width && h == m_height) {
                if (tjDecompress2(m_tjDecompressor, const_cast<unsigned char*>(rawData), bytesUsed,
                                 outRgba.data(), m_width, 0, m_height, TJPF_RGBA, TJFLAG_FASTDCT) == 0) {
                    decoded = true;
                }
            }
        }
    } else if (m_pixelFormat == V4L2_PIX_FMT_YUYV) {
        yuyvToRgba(rawData, outRgba.data(), m_width, m_height);
        decoded = true;
    }

    // Re-queue the latest buffer back to kernel
    xioctl(m_fd, VIDIOC_QBUF, &latestBuf);
    return decoded;
}

std::vector<CameraControlInfo> V4L2Capture::getAvailableControls() {
    std::vector<CameraControlInfo> list;
    if (m_fd < 0) return list;

    // Controls of interest
    std::vector<uint32_t> controlIds = {
        V4L2_CID_BRIGHTNESS,
        V4L2_CID_CONTRAST,
        V4L2_CID_SATURATION,
        V4L2_CID_GAIN,
        V4L2_CID_SHARPNESS,
        V4L2_CID_BACKLIGHT_COMPENSATION,
        V4L2_CID_EXPOSURE_AUTO,
        V4L2_CID_EXPOSURE_ABSOLUTE,
        V4L2_CID_AUTO_WHITE_BALANCE,
        V4L2_CID_WHITE_BALANCE_TEMPERATURE
    };

    for (uint32_t id : controlIds) {
        v4l2_queryctrl qctrl;
        std::memset(&qctrl, 0, sizeof(qctrl));
        qctrl.id = id;

        if (xioctl(m_fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
            if (qctrl.flags & V4L2_CTRL_FLAG_DISABLED) continue;

            CameraControlInfo info;
            info.id = qctrl.id;
            info.name = reinterpret_cast<char*>(qctrl.name);
            info.min = qctrl.minimum;
            info.max = qctrl.maximum;
            info.step = qctrl.step;
            info.def = qctrl.default_value;
            info.current = getControl(id);
            list.push_back(info);
        }
    }

    return list;
}

bool V4L2Capture::setControl(uint32_t id, int32_t value) {
    if (m_fd < 0) return false;
    v4l2_control ctrl;
    std::memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    ctrl.value = value;
    return (xioctl(m_fd, VIDIOC_S_CTRL, &ctrl) == 0);
}

int32_t V4L2Capture::getControl(uint32_t id) {
    if (m_fd < 0) return 0;
    v4l2_control ctrl;
    std::memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    if (xioctl(m_fd, VIDIOC_G_CTRL, &ctrl) == 0) {
        return ctrl.value;
    }
    return 0;
}
