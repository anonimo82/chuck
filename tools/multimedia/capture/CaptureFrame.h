#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
#include <utility>

namespace media_capture {
// Shared canonical frame: top-down, tightly packed straight RGBA8.
struct Frame {
    int width=0,height=0;
    double seconds=0;
    std::vector<unsigned char> rgba;
    static bool dimensions(int64_t w,int64_t h) {
        return w>=1&&h>=1&&w<=8192&&h<=8192&&w*h<=4194304;
    }
    bool valid() const {
        return dimensions(width,height)&&rgba.size()==static_cast<size_t>(width)*height*4&&
            std::isfinite(seconds)&&seconds>=0;
    }
};
enum class Source { none=0, camera=1, screen=2 };
enum class Backend { none=0, windows=1, linux=2, macos=3 };
inline Backend platform() {
#if defined(_WIN32)
    return Backend::windows;
#elif defined(__APPLE__)
    return Backend::macos;
#elif defined(__linux__)
    return Backend::linux;
#else
    return Backend::none;
#endif
}
// Backend availability is not inferred from the operating system.
struct Device {
    Source source=Source::none;
    Backend backend=Backend::none;
};
}
