#include "Ddc.h"

#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#include <algorithm>
#include <map>

namespace
{
// DDC/CI is a slow I2C link and monitors drop commands that arrive too quickly; retry a little.
constexpr int kAttempts = 3;
constexpr DWORD kRetryDelayMs = 50;

struct Found
{
    LONG left = 0;
    DdcMonitor monitor;
};

// GDI device name ("\\.\DISPLAY1") -> the monitor's EDID name ("DELL U2720Q"). The physical
// monitor description from dxva2 is usually just "Generic PnP Monitor".
std::map<std::wstring, std::wstring> FriendlyNames()
{
    std::map<std::wstring, std::wstring> names;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG result = ERROR_SUCCESS;
    do
    {
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            return names;
        paths.resize(pathCount);
        modes.resize(modeCount);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        paths.resize(pathCount);
    } while (result == ERROR_INSUFFICIENT_BUFFER);
    if (result != ERROR_SUCCESS)
        return names;

    for (const DISPLAYCONFIG_PATH_INFO& path : paths)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;

        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
            DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS && target.monitorFriendlyDeviceName[0] != L'\0')
            names.emplace(source.viewGdiDeviceName, target.monitorFriendlyDeviceName);
    }
    return names;
}

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context)
{
    reinterpret_cast<std::vector<HMONITOR>*>(context)->push_back(monitor);
    return TRUE;
}
} // namespace

bool ReadVcp(HANDLE physical, BYTE code, VcpValue& value)
{
    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        if (attempt > 0)
            Sleep(kRetryDelayMs);
        DWORD current = 0;
        DWORD max = 0;
        if (GetVCPFeatureAndVCPFeatureReply(physical, code, nullptr, &current, &max) && max != 0)
        {
            value = { .current = current, .max = max };
            return true;
        }
    }
    return false;
}

bool WriteVcp(HANDLE physical, BYTE code, DWORD value)
{
    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        if (attempt > 0)
            Sleep(kRetryDelayMs);
        if (SetVCPFeature(physical, code, value))
            return true;
    }
    return false;
}

std::vector<DdcMonitor> EnumerateDdcMonitors()
{
    std::vector<HMONITOR> handles;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&handles));
    const std::map<std::wstring, std::wstring> friendlyNames = FriendlyNames();

    std::vector<Found> found;
    for (HMONITOR handle : handles)
    {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(handle, &info))
            continue;

        DWORD count = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(handle, &count) || count == 0)
            continue;
        std::vector<PHYSICAL_MONITOR> physical(count);
        if (!GetPhysicalMonitorsFromHMONITOR(handle, count, physical.data()))
            continue;

        for (const PHYSICAL_MONITOR& entry : physical)
        {
            DdcMonitor monitor;
            monitor.physical = entry.hPhysicalMonitor;
            if (!ReadVcp(monitor.physical, kVcpBrightness, monitor.brightness))
            {
                DestroyPhysicalMonitor(monitor.physical);
                continue;
            }
            monitor.hasContrast = ReadVcp(monitor.physical, kVcpContrast, monitor.contrast);

            // A cloned display has several physical monitors behind one GDI device; the EDID name
            // then can't be matched to a specific one.
            const auto friendly = friendlyNames.find(info.szDevice);
            monitor.name = friendly != friendlyNames.end() && count == 1 ? friendly->second
                                                                          : std::wstring(entry.szPhysicalMonitorDescription);
            found.push_back({ .left = info.rcMonitor.left, .monitor = std::move(monitor) });
        }
    }

    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.left < b.left; });
    std::vector<DdcMonitor> monitors;
    for (Found& entry : found)
        monitors.push_back(std::move(entry.monitor));
    return monitors;
}

void DestroyDdcMonitors(std::vector<DdcMonitor>& monitors)
{
    for (const DdcMonitor& monitor : monitors)
        DestroyPhysicalMonitor(monitor.physical);
    monitors.clear();
}
