# Camera Processor & Virtual V4L2 Device

A high-performance real-time webcam video processing and calibration tool written in C++17, OpenGL/GLSL, and Qt6.

It captures video from a physical camera (e.g. `/dev/video0`), applies GPU-accelerated image filters, custom tone curve adjustments, interactive fixed-aspect cropping, and direct V4L2 sensor-level controls with side-by-side pre/post preview panes. The resulting stream is piped directly into a Linux virtual loopback camera (`/dev/video10`), making it instantly accessible to web browsers (Chrome, Firefox), video conferencing software (Google Meet, Zoom, Teams), OBS, and Discord.

---

## Key Features

### 1. Dual Real-Time Previews & Interactive Aspect Crop
- **Side-by-Side Panes**: Left pane shows the raw input camera stream; right pane shows the processed, color-graded, and cropped output.
- **Aspect-Locked Crop Overlay**:
  - Interactive crop box overlayed directly on top of the input video with drag handles (corners and edges) and rule-of-thirds grid lines.
  - Automatically locks to the virtual camera device's output aspect ratio (e.g. 16:9).
  - Cropped region is dynamically scaled and centered on the GPU before streaming out.

### 2. GPU Accelerated GLSL Shaders
- **Tone & Contrast Curve Editor**:
  - Interactive cubic spline curve editor with 256-entry 1D LUT mapping on the GPU.
  - One-click presets: *Linear*, *S-Curve*, *Lift Shadows*.
- **Temporal Noise Reduction (TNR)**:
  - Multi-frame 3-frame sliding window temporal filter executed on the GPU.
  - Motion-adaptive blending: suppresses temporal smoothing over moving objects to eliminate motion ghosting while aggressively averaging out sensor noise and grain across static regions.
  - Adjustable TNR strength slider and toggle.
- **Color Temperature / White Balance**: Real-time warm/cool color balance compensation.
- **Brightness & Contrast**: Mid-tone centered contrast adjustments.
- **Gamma Correction**: Non-linear power-law gamma curve.
- **Saturation**: High-fidelity HSV color space saturation adjustment.
- **Sharpness**: Spatial convolution unsharp masking kernel on GPU.
- **Mirroring**: Horizontal flip toggle for natural mirrored previewing.

### 3. Interactive Color Sampling
- **Point Sampling Tools**: Calibrate color balance by sampling pixels directly from the input image stream:
  - **Sample Black**: Anchors the black point / shadow threshold of the tone curve.
  - **Sample Grey**: Anchors the tone curve midpoint and trims temperature tint.
  - **Sample White**: Sets the highlight clipping point and calculates temperature offsets for neutral white balance.
- **Real-Time Color Swatch**: Displays sampled RGB values, luminance, and hex color preview.

### 4. Input Device Controls & Auto Scene Presets
- **Hardware Sensor Sliders**: Direct V4L2 ioctl controls for physical camera hardware (exposure, gain, brightness, contrast, saturation, sharpness, and backlight compensation).
- **Auto Scene Optimization**:
  - **Dark Scene**: Boosts sensor hardware gain and brightness while softening contrast for low-light environments.
  - **Neutral Scene**: Restores manufacturer default sensor calibration.
  - **Bright Scene**: Reduces gain, enables backlight compensation, and increases contrast to eliminate glare and blown highlights.

### 5. Seamless V4L2 Loopback Integration & Auto-Installer
- Streams YUYV frames into `/dev/video10` using `v4l2loopback`.
- **Integrated Loopback Helper**: If `/dev/video10` is missing, an in-app helper prompts to load `v4l2loopback` via `pkexec modprobe` with the proper device options (`video_nr=10 card_label=CameraProcessor exclusive_caps=1`), enabling zero-configuration setup.

---

## Dependencies

On Debian/Ubuntu-based systems (Ubuntu 22.04 / 24.04+):
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    qt6-base-dev \
    libgl-dev \
    libturbojpeg-dev \
    v4l2loopback-dkms \
    v4l-utils \
    policykit-1
```

---

## Building and Running

### 1. Build with CMake and Ninja
```bash
cmake -B build -G Ninja
cmake --build build
```

### 2. Run
```bash
./build/camera-processor
```

If `/dev/video10` is not yet loaded, click the **"Install /dev/video10"** button in the bottom status bar, or manually load the kernel module:
```bash
sudo modprobe v4l2loopback video_nr=10 card_label="CameraProcessor" exclusive_caps=1
```

### 3. Use in Applications
Select **"CameraProcessor"** (`/dev/video10`) in Google Meet, Zoom, Microsoft Teams, Discord, OBS, or Chromium/Firefox settings.

---

## Project Structure

```
camera-processor/
├── CMakeLists.txt              # CMake build configuration
├── README.md                   # Documentation and usage guide
├── .gitignore                  # Git ignore rules
└── src/
    ├── main.cpp                # Application entry point
    ├── main_window.h/.cpp      # Main GUI window, layout, and orchestration
    ├── gl_processor.h/.cpp     # OpenGL shader pipeline, FBO offscreen rendering, and texture handling
    ├── crop_overlay.h/.cpp     # Interactive crop widget, handles, aspect constraint, and color sampling
    ├── curve_editor.h/.cpp     # Interactive spline-based tone curve editor & LUT generator
    ├── v4l2_capture.h/.cpp     # Physical camera capture (V4L2 + TurboJPEG MJPEG decompression)
    └── v4l2_output.h/.cpp      # Virtual loopback output device streaming (V4L2 YUYV)
```

---

## License

MIT License. See LICENSE for details.
