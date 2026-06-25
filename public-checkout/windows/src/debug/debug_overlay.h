#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "metrics_collector.h"
#include <windows.h>
#include <string>

namespace morphic {

// Visual debug overlay — mandatory from day 1.
// Renders as a topmost transparent HWND with GDI.
// Shows bounds, elevation, frame metrics, surface IDs.
class RuntimeCommitScheduler;

class DebugOverlay {
public:
    DebugOverlay() = default;
    ~DebugOverlay() { destroy(); }

    void initialize();
    void destroy();

    bool isEnabled() const { return enabled_; }
    void setEnabled(bool enabled);

    void showBounds(bool show) { showBounds_ = show; }
    void showElevation(bool show) { showElevation_ = show; }
    void showMetrics(bool show) { showMetrics_ = show; }

    // Called each frame to update the overlay
    void render(const SceneGraph& graph, const MetricsCollector& metrics, RuntimeCommitScheduler* scheduler = nullptr);

private:
    HWND hwnd_ = nullptr;
    bool enabled_ = false;
    bool showBounds_ = true;
    bool showElevation_ = true;
    bool showMetrics_ = true;

    static constexpr wchar_t kDebugClassName[] = L"MorphicDebugOverlay";
    static bool classRegistered_;

    void createWindow();
    void paintOverlay(HDC hdc, const SceneGraph& graph, const MetricsCollector& metrics, RuntimeCommitScheduler* scheduler = nullptr);

    static LRESULT CALLBACK DebugWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

}  // namespace morphic
