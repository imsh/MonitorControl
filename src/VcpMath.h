#pragma once

#include <algorithm>
#include <cstdint>

constexpr uint8_t kVcpBrightness = 0x10;
constexpr uint8_t kVcpContrast = 0x12;

// Monitors report each VCP feature as 0..max (max is usually 100, but not always); the UI works in percent.

constexpr uint32_t PercentToRaw(int percent, uint32_t max)
{
    const uint64_t clamped = static_cast<uint64_t>(std::clamp(percent, 0, 100));
    return static_cast<uint32_t>((clamped * max + 50) / 100);
}

constexpr int RawToPercent(uint32_t raw, uint32_t max)
{
    if (max == 0)
        return 0;
    const uint64_t clamped = std::min(raw, max);
    return static_cast<int>((clamped * 100 + max / 2) / max);
}
