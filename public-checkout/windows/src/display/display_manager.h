#pragma once

#include "../core/types.h"
#include <windows.h>
#include <shellscalingapi.h>
#include <vector>

#pragma comment(lib, "shcore.lib")

namespace morphic {

// Tracks monitor topology: DPI, scaling, work areas, refresh rates.
class DisplayManager {
public:
    void refresh() {
        if (simulated_) return;
        displays_.clear();
        EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                           reinterpret_cast<LPARAM>(this));
    }

    void setMockDisplays(const std::vector<DisplayInfo>& mockDisplays) {
        simulated_ = true;
        displays_ = mockDisplays;
    }

    void clearMockDisplays() {
        simulated_ = false;
        refresh();
    }

    bool isSimulated() const { return simulated_; }

    const std::vector<DisplayInfo>& displays() const { return displays_; }

    DisplayInfo* getDisplayForPoint(POINT pt) {
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        DisplayInfo* d = findDisplay(hMon);
        return d ? d : getPrimaryDisplay();
    }

    DisplayInfo* getDisplayForRect(const RECT& rc) {
        HMONITOR hMon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
        DisplayInfo* d = findDisplay(hMon);
        return d ? d : getPrimaryDisplay();
    }

    DisplayInfo* getPrimaryDisplay() {
        for (auto& d : displays_) {
            if (d.isPrimary) return &d;
        }
        return displays_.empty() ? nullptr : &displays_[0];
    }

private:
    std::vector<DisplayInfo> displays_;
    bool simulated_ = false;

    DisplayInfo* findDisplay(HMONITOR hMon) {
        for (auto& d : displays_) {
            if (d.handle == hMon) return &d;
        }
        return nullptr;
    }

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM dwData) {
        auto* self = reinterpret_cast<DisplayManager*>(dwData);

        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(MONITORINFOEXW);
        if (!GetMonitorInfoW(hMonitor, &mi)) return TRUE;

        DisplayInfo info;
        info.handle = hMonitor;
        info.bounds = mi.rcMonitor;
        info.workArea = mi.rcWork;
        info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        info.deviceName = mi.szDevice;

        // Get DPI
        UINT dpiX = 96, dpiY = 96;
        if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
            info.dpiX = static_cast<float>(dpiX);
            info.dpiY = static_cast<float>(dpiY);
            info.scaleFactor = info.dpiX / 96.0f;
        }

        // Get refresh rate
        DEVMODEW dm = {};
        dm.dmSize = sizeof(DEVMODEW);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
            info.refreshRate = dm.dmDisplayFrequency;
        }

        self->displays_.push_back(info);
        return TRUE;
    }
};

}  // namespace morphic
