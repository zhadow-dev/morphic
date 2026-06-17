#pragma once

#include "../core/types.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <chrono>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

namespace morphic {

// Phase 2A.3 — GPU Pressure Profiler.
//
// Non-invasive GPU resource measurement using DXGI APIs.
// Determines VRAM usage patterns across engine lifecycle events.
//
// What it measures:
//   1. Process-level GPU memory (dedicated VRAM, shared system memory)
//   2. Per-adapter budget vs usage
//   3. VRAM snapshots at key lifecycle events (attach, detach, zombify)
//   4. VRAM scaling curve (delta per engine)
//   5. VRAM decay after engine hide/zombify
//
// Design principles:
//   - Uses IDXGIAdapter3::QueryVideoMemoryInfo (Windows 10+)
//   - Falls back to IDXGIAdapter::GetDesc for older systems
//   - Non-invasive: queries only, never allocates GPU resources
//   - Timestamps every snapshot for temporal correlation
//
// Key questions this system answers:
//   - Does VRAM grow monotonically with engine count?
//   - Does hiding/zombifying an engine release GPU memory?
//   - What is the VRAM slope (linear, sublinear, superlinear)?
//   - Does idle VRAM stabilize or continue growing?
class GpuProfiler {
public:

    // Single VRAM snapshot.
    struct VramSnapshot {
        std::chrono::high_resolution_clock::time_point timestamp;
        std::string label;           // "baseline", "post_attach_1", "post_zombify", etc.

        // DXGI budget info (IDXGIAdapter3)
        bool hasBudgetInfo = false;
        uint64_t dedicatedBudgetBytes = 0;    // OS-assigned VRAM budget
        uint64_t dedicatedUsageBytes = 0;     // Current VRAM usage
        uint64_t sharedBudgetBytes = 0;       // Shared system memory budget
        uint64_t sharedUsageBytes = 0;        // Shared system memory usage

        // Adapter description
        std::string adapterName;
        uint64_t adapterDedicatedVideoMemory = 0;   // Total dedicated VRAM
        uint64_t adapterDedicatedSystemMemory = 0;
        uint64_t adapterSharedSystemMemory = 0;
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;

        // Derived
        double dedicatedUsagePercent = 0.0;   // usage / budget * 100
        double dedicatedUsageMB = 0.0;
        double sharedUsageMB = 0.0;
    };

    // Delta between two VRAM snapshots.
    struct VramDelta {
        double elapsedMs = 0.0;
        std::string fromLabel;
        std::string toLabel;

        int64_t dedicatedDeltaBytes = 0;
        int64_t sharedDeltaBytes = 0;
        double dedicatedDeltaMB = 0.0;
        double sharedDeltaMB = 0.0;
        double dedicatedUsagePercentDelta = 0.0;
    };

    // VRAM scaling curve point.
    struct ScalingPoint {
        int engineCount = 0;
        VramSnapshot snapshot;
        VramDelta deltaFromBaseline;
    };

    // Full profiling result.
    struct ProfileResult {
        bool available = false;          // false if DXGI not available
        std::string adapterName;

        VramSnapshot baseline;
        std::vector<VramSnapshot> snapshots;
        std::vector<VramDelta> deltas;
        std::vector<ScalingPoint> scalingCurve;

        // Summary
        double totalDedicatedDeltaMB = 0.0;  // Total VRAM change from baseline
        double totalSharedDeltaMB = 0.0;
        double avgDedicatedPerEngineMB = 0.0;
        bool vramGrowthMonotonic = false;     // Kill condition check
        bool vramStabilizedAfterHide = false;

        std::string details;
    };

    GpuProfiler() {
        initializeDXGI();
    }

    bool isAvailable() const { return adapter3_ != nullptr || adapter_ != nullptr; }

    // --- Snapshot capture ---

    VramSnapshot captureSnapshot(const std::string& label = "") const {
        VramSnapshot snap;
        snap.timestamp = std::chrono::high_resolution_clock::now();
        snap.label = label;

        if (adapter3_) {
            // Use IDXGIAdapter3 for budget-aware queries (Windows 10+)
            DXGI_QUERY_VIDEO_MEMORY_INFO dedicatedInfo = {};
            DXGI_QUERY_VIDEO_MEMORY_INFO sharedInfo = {};

            HRESULT hr1 = adapter3_->QueryVideoMemoryInfo(
                0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &dedicatedInfo);
            HRESULT hr2 = adapter3_->QueryVideoMemoryInfo(
                0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &sharedInfo);

            if (SUCCEEDED(hr1)) {
                snap.hasBudgetInfo = true;
                snap.dedicatedBudgetBytes = dedicatedInfo.Budget;
                snap.dedicatedUsageBytes = dedicatedInfo.CurrentUsage;
            }
            if (SUCCEEDED(hr2)) {
                snap.sharedBudgetBytes = sharedInfo.Budget;
                snap.sharedUsageBytes = sharedInfo.CurrentUsage;
            }
        }

        // Always fill adapter description
        if (adapterDesc_.DedicatedVideoMemory > 0) {
            // Convert wide string to narrow
            char name[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, adapterDesc_.Description, -1,
                name, sizeof(name), nullptr, nullptr);
            snap.adapterName = name;
            snap.adapterDedicatedVideoMemory = adapterDesc_.DedicatedVideoMemory;
            snap.adapterDedicatedSystemMemory = adapterDesc_.DedicatedSystemMemory;
            snap.adapterSharedSystemMemory = adapterDesc_.SharedSystemMemory;
            snap.vendorId = adapterDesc_.VendorId;
            snap.deviceId = adapterDesc_.DeviceId;
        }

        // Derived metrics
        snap.dedicatedUsageMB = static_cast<double>(snap.dedicatedUsageBytes) / (1024.0 * 1024.0);
        snap.sharedUsageMB = static_cast<double>(snap.sharedUsageBytes) / (1024.0 * 1024.0);
        if (snap.dedicatedBudgetBytes > 0) {
            snap.dedicatedUsagePercent =
                static_cast<double>(snap.dedicatedUsageBytes) /
                static_cast<double>(snap.dedicatedBudgetBytes) * 100.0;
        }

        return snap;
    }

    VramDelta computeDelta(const VramSnapshot& from, const VramSnapshot& to) const {
        VramDelta d;
        d.elapsedMs = std::chrono::duration<double, std::milli>(
            to.timestamp - from.timestamp).count();
        d.fromLabel = from.label;
        d.toLabel = to.label;

        d.dedicatedDeltaBytes = static_cast<int64_t>(to.dedicatedUsageBytes) -
                                static_cast<int64_t>(from.dedicatedUsageBytes);
        d.sharedDeltaBytes = static_cast<int64_t>(to.sharedUsageBytes) -
                             static_cast<int64_t>(from.sharedUsageBytes);
        d.dedicatedDeltaMB = static_cast<double>(d.dedicatedDeltaBytes) / (1024.0 * 1024.0);
        d.sharedDeltaMB = static_cast<double>(d.sharedDeltaBytes) / (1024.0 * 1024.0);
        d.dedicatedUsagePercentDelta = to.dedicatedUsagePercent - from.dedicatedUsagePercent;

        return d;
    }

    // --- Full profiling run ---
    // Captures baseline, then monitors VRAM over decayCount intervals.
    // Call this AFTER engines have been attached/zombified to observe behavior.
    ProfileResult runProfile(int decayCount = 5, int decayIntervalMs = 1000) const {
        ProfileResult result;
        result.available = isAvailable();

        if (!result.available) {
            result.details = "DXGI not available — cannot profile GPU";
            return result;
        }

        // Baseline
        result.baseline = captureSnapshot("baseline");
        result.adapterName = result.baseline.adapterName;

        // Capture decay/stability curve
        VramSnapshot previous = result.baseline;
        bool monotonic = true;

        for (int i = 0; i < decayCount; i++) {
            Sleep(static_cast<DWORD>(decayIntervalMs));

            // Keep message pump alive
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            auto snap = captureSnapshot("decay_" + std::to_string(i));
            auto delta = computeDelta(previous, snap);
            auto deltaFromBaseline = computeDelta(result.baseline, snap);

            result.snapshots.push_back(snap);
            result.deltas.push_back(delta);

            ScalingPoint sp;
            sp.engineCount = i;
            sp.snapshot = snap;
            sp.deltaFromBaseline = deltaFromBaseline;
            result.scalingCurve.push_back(sp);

            // Check monotonic growth
            if (delta.dedicatedDeltaBytes < 0) {
                monotonic = false;  // VRAM decreased — good sign
            }

            previous = snap;
        }

        result.vramGrowthMonotonic = monotonic;

        // Final delta from baseline
        if (!result.snapshots.empty()) {
            auto finalDelta = computeDelta(result.baseline, result.snapshots.back());
            result.totalDedicatedDeltaMB = finalDelta.dedicatedDeltaMB;
            result.totalSharedDeltaMB = finalDelta.sharedDeltaMB;

            // Check if VRAM stabilized (last 2 snapshots within 1MB)
            if (result.snapshots.size() >= 2) {
                auto& secondLast = result.snapshots[result.snapshots.size() - 2];
                auto& last = result.snapshots.back();
                double diff = std::abs(last.dedicatedUsageMB - secondLast.dedicatedUsageMB);
                result.vramStabilizedAfterHide = (diff < 1.0);
            }
        }

        // Build details
        result.details = "Adapter=" + result.adapterName +
            " DedicatedVRAM=" + std::to_string(
                result.baseline.adapterDedicatedVideoMemory / (1024 * 1024)) + "MB" +
            " BaselineUsage=" + std::to_string(result.baseline.dedicatedUsageMB) + "MB" +
            " FinalUsage=" + (result.snapshots.empty() ? "N/A" :
                std::to_string(result.snapshots.back().dedicatedUsageMB) + "MB") +
            " DeltaMB=" + std::to_string(result.totalDedicatedDeltaMB) +
            " Monotonic=" + (result.vramGrowthMonotonic ? "YES" : "NO") +
            " Stabilized=" + (result.vramStabilizedAfterHide ? "YES" : "NO");

        if (result.baseline.hasBudgetInfo) {
            result.details += " BudgetUsage=" +
                std::to_string(result.baseline.dedicatedUsagePercent) + "%";
        }

        OutputDebugStringA(("GPU_PROFILE: " + result.details + "\n").c_str());

        return result;
    }

private:
    void initializeDXGI() {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            OutputDebugStringA("GPU_PROFILE: CreateDXGIFactory1 failed\n");
            return;
        }

        // Get first adapter (primary GPU)
        hr = factory->EnumAdapters(0, &adapter_);
        if (FAILED(hr) || !adapter_) {
            OutputDebugStringA("GPU_PROFILE: No DXGI adapter found\n");
            return;
        }

        // Get adapter description
        adapter_->GetDesc(&adapterDesc_);

        // Try to get IDXGIAdapter3 for budget queries (Windows 10+)
        hr = adapter_.As(&adapter3_);
        if (SUCCEEDED(hr) && adapter3_) {
            OutputDebugStringA("GPU_PROFILE: IDXGIAdapter3 available (budget queries)\n");
        } else {
            OutputDebugStringA("GPU_PROFILE: IDXGIAdapter3 not available (basic mode)\n");
            adapter3_ = nullptr;
        }

        char name[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, adapterDesc_.Description, -1,
            name, sizeof(name), nullptr, nullptr);
        OutputDebugStringA(("GPU_PROFILE: Adapter=" + std::string(name) +
            " DedicatedVRAM=" + std::to_string(adapterDesc_.DedicatedVideoMemory / (1024*1024)) +
            "MB\n").c_str());
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter_;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
    DXGI_ADAPTER_DESC adapterDesc_ = {};
};

}  // namespace morphic
