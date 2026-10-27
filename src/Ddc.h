#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "VcpMath.h"

// DDC/CI over the Monitor Configuration API (dxva2). Every call blocks for tens of milliseconds,
// so these run on DdcWorker's thread (or in a command-line run, when no tray app is running).

struct VcpValue
{
    DWORD current = 0;
    DWORD max = 0;
};

struct DdcMonitor
{
    HANDLE physical = nullptr;
    std::wstring name;
    VcpValue brightness;
    VcpValue contrast;
    bool hasContrast = false;
};

// Monitors that answer a brightness read, left to right. Others (a laptop panel, DDC/CI switched
// off in the monitor's menu) are skipped. Release the result with DestroyDdcMonitors().
std::vector<DdcMonitor> EnumerateDdcMonitors();
void DestroyDdcMonitors(std::vector<DdcMonitor>& monitors);

bool ReadVcp(HANDLE physical, BYTE code, VcpValue& value);
bool WriteVcp(HANDLE physical, BYTE code, DWORD value);
