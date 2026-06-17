#include "debug_overlay.h"
#include "../core/runtime_commit_scheduler.h"
#include "../core/kernel_trace.h"
#include "../core/composition_surface.h"
#include <cstdio>
#include <windows.h>

namespace morphic {

bool DebugOverlay::classRegistered_ = false;

void DebugOverlay::initialize() {
    if (!classRegistered_) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DebugWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kDebugClassName;
        RegisterClassExW(&wc);
        classRegistered_ = true;
    }
}

void DebugOverlay::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void DebugOverlay::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (enabled && !hwnd_) {
        createWindow();
    } else if (!enabled && hwnd_) {
        destroy();
    }
}

void DebugOverlay::createWindow() {
    // Get virtual screen bounds (all monitors)
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd_ = CreateWindowExW(
        // Compositor-local overlay — NOT WS_EX_TOPMOST.
        // WS_EX_TOPMOST would contaminate the z-order band: Windows uses
        // topmost windows as z-anchors, pulling normal surfaces into the
        // global topmost band during SetWindowPos(HWND_TOP) realization.
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        kDebugClassName,
        L"Morphic Debug",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (hwnd_) {
        // Make fully transparent by default — we paint with UpdateLayeredWindow
        SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
}

void DebugOverlay::render(const SceneGraph& graph, const MetricsCollector& metrics, RuntimeCommitScheduler* scheduler) {
    if (!enabled_ || !hwnd_) return;

    // Get overlay dimensions
    RECT overlayRect;
    GetWindowRect(hwnd_, &overlayRect);
    int ow = overlayRect.right - overlayRect.left;
    int oh = overlayRect.bottom - overlayRect.top;

    // Double-buffered paint
    HDC screenDC = GetDC(hwnd_);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bmp = CreateCompatibleBitmap(screenDC, ow, oh);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

    // Clear with color key (transparent)
    HBRUSH clearBrush = CreateSolidBrush(RGB(0, 0, 0));
    RECT fullRect = { 0, 0, ow, oh };
    FillRect(memDC, &fullRect, clearBrush);
    DeleteObject(clearBrush);

    paintOverlay(memDC, graph, metrics, scheduler);

    // Blit to screen
    BitBlt(screenDC, 0, 0, ow, oh, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(hwnd_, screenDC);
}

void DebugOverlay::paintOverlay(HDC hdc, const SceneGraph& graph, const MetricsCollector& metrics, RuntimeCommitScheduler* scheduler) {
    RECT overlayRect;
    GetWindowRect(hwnd_, &overlayRect);
    int offsetX = overlayRect.left;
    int offsetY = overlayRect.top;

    // --- Draw surface bounds ---
    if (showBounds_) {
        HPEN boundsPen = CreatePen(PS_SOLID, 2, RGB(0, 255, 100));
        HPEN oldPen = (HPEN)SelectObject(hdc, boundsPen);
        HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);

        graph.forEachSurface([&](const CompositionSurface* s) {
            auto& t = s->worldTransform();
            
            // Draw realized boundary in sleek green/blue
            HPEN surfacePen = CreatePen(PS_SOLID, 2, RGB(0, 255, 120));
            HPEN prevPen = (HPEN)SelectObject(hdc, surfacePen);
            
            Rectangle(hdc,
                      t.x - offsetX, t.y - offsetY,
                      t.x + t.width - offsetX, t.y + t.height - offsetY);
                      
            SelectObject(hdc, prevPen);
            DeleteObject(surfacePen);

            // Draw Desired boundary & drift vector if scheduler is active and has divergence
            if (scheduler) {
                const auto* state = scheduler->sceneState().getCommittedState(s->id());
                if (state) {
                    bool diverged = state->hasGeometryDivergence || 
                                    t.x != state->desiredGeometry.x || 
                                    t.y != state->desiredGeometry.y || 
                                    t.width != state->desiredGeometry.width || 
                                    t.height != state->desiredGeometry.height;
                    
                    if (diverged) {
                        COLORREF divColor = RGB(255, 220, 0); // Amber by default
                        if (state->severity == DivergenceSeverity::Critical) {
                            divColor = RGB(255, 100, 0);
                        } else if (state->severity == DivergenceSeverity::Terminal) {
                            divColor = RGB(255, 50, 50);
                        }

                        HPEN divPen = CreatePen(PS_DOT, 1, divColor);
                        HPEN oldDivPen = (HPEN)SelectObject(hdc, divPen);

                        // 1. Draw desired geometry as dotted amber/red box
                        auto& d = state->desiredGeometry;
                        Rectangle(hdc,
                                  d.x - offsetX, d.y - offsetY,
                                  d.x + d.width - offsetX, d.y + d.height - offsetY);

                        // 2. Draw drift vector connecting centers
                        int cxR = t.x + t.width / 2;
                        int cyR = t.y + t.height / 2;
                        int cxD = d.x + d.width / 2;
                        int cyD = d.y + d.height / 2;

                        MoveToEx(hdc, cxR - offsetX, cyR - offsetY, NULL);
                        LineTo(hdc, cxD - offsetX, cyD - offsetY);

                        SelectObject(hdc, oldDivPen);
                        DeleteObject(divPen);
                    }

                    // 3. Draw Quarantine Marker
                    if (state->presence == RuntimePresence::Quarantined) {
                        HPEN quarantinePen = CreatePen(PS_SOLID, 4, RGB(255, 30, 30));
                        HPEN oldQPen = (HPEN)SelectObject(hdc, quarantinePen);

                        // Draw big red X
                        MoveToEx(hdc, t.x - offsetX, t.y - offsetY, NULL);
                        LineTo(hdc, t.x + t.width - offsetX, t.y + t.height - offsetY);
                        MoveToEx(hdc, t.x + t.width - offsetX, t.y - offsetY, NULL);
                        LineTo(hdc, t.x - offsetX, t.y + t.height - offsetY);

                        SelectObject(hdc, oldQPen);
                        DeleteObject(quarantinePen);

                        // Text overlay inside the surface
                        SetTextColor(hdc, RGB(255, 50, 50));
                        wchar_t qMsg[] = L"[QUARANTINED]";
                        TextOutW(hdc, t.x - offsetX + 8, t.y - offsetY + 8, qMsg, (int)wcslen(qMsg));
                    }
                }
            }
        });

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(boundsPen);
    }

    // --- Draw elevation labels ---
    if (showElevation_) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 0));

        graph.forEachSurface([&](const CompositionSurface* s) {
            auto& t = s->worldTransform();
            wchar_t label[128];
            
            if (scheduler) {
                const auto* state = scheduler->sceneState().getCommittedState(s->id());
                uint64_t epoch = scheduler->getSurfaceEpoch(s->id());
                
                if (state) {
                    const char* presStr = "Budgeted";
                    if (state->presence == RuntimePresence::ThrottledFrameRate) presStr = "Throttled";
                    else if (state->presence == RuntimePresence::Hibernating) presStr = "Hibernating";
                    else if (state->presence == RuntimePresence::Quarantined) presStr = "Quarantined";

                    const char* sevStr = "Transient";
                    if (state->severity == DivergenceSeverity::Persistent) sevStr = "Persistent";
                    else if (state->severity == DivergenceSeverity::Critical) sevStr = "Critical";
                    else if (state->severity == DivergenceSeverity::Terminal) sevStr = "Terminal";

                    swprintf_s(label, L"#%u E:%d | Ep:%llu | Age:%d | %hs (%hs)", 
                        s->id(), 
                        static_cast<int>(s->elevationLayer()),
                        epoch,
                        state->divergenceTicks,
                        presStr,
                        sevStr);
                } else {
                    swprintf_s(label, L"#%u E:%d | Ep:%llu", s->id(), static_cast<int>(s->elevationLayer()), epoch);
                }
            } else {
                swprintf_s(label, L"#%u E:%d", s->id(), static_cast<int>(s->elevationLayer()));
            }
            
            TextOutW(hdc, t.x - offsetX + 4, t.y + t.height - 20 - offsetY, label, (int)wcslen(label));
        });
    }

    // --- Draw metrics (Redesigned Health Dashboard Card) ---
    if (showMetrics_) {
        SetBkMode(hdc, TRANSPARENT);

        // Draw background card for the dashboard
        HBRUSH bgBrush = CreateSolidBrush(RGB(20, 20, 25));
        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 110));
        HBRUSH oldBgBrush = (HBRUSH)SelectObject(hdc, bgBrush);
        HPEN oldBorderPen = (HPEN)SelectObject(hdc, borderPen);
        
        RoundRect(hdc, 10, 10, 520, 220, 10, 10);
        
        SelectObject(hdc, oldBgBrush);
        SelectObject(hdc, oldBorderPen);
        DeleteObject(bgBrush);
        DeleteObject(borderPen);

        // Header
        SetTextColor(hdc, RGB(255, 255, 255));
        wchar_t headerText[] = L"MORPHIC KERNEL HEALTH DASHBOARD";
        TextOutW(hdc, 20, 20, headerText, (int)wcslen(headerText));

        if (scheduler) {
            auto& health = scheduler->health();
            COLORREF badgeColor = RGB(0, 255, 100); // High
            if (health.confidence == KernelConfidence::Degraded) {
                badgeColor = RGB(255, 220, 0); // Degraded
            } else if (health.confidence == KernelConfidence::Uncertain) {
                badgeColor = RGB(255, 50, 50); // Uncertain
            }
            
            SetTextColor(hdc, badgeColor);
            wchar_t badgeText[128];
            swprintf_s(badgeText, L"CONFIDENCE: %hs", toString(health.confidence));
            TextOutW(hdc, 330, 20, badgeText, (int)wcslen(badgeText));
            
            // Render 4 orthogonal dimensions
            SetTextColor(hdc, health.temporal >= 0.95 ? RGB(0, 255, 120) : RGB(255, 100, 100));
            wchar_t tempText[128];
            swprintf_s(tempText, L"TEMPORAL: %.2f", health.temporal);
            TextOutW(hdc, 20, 50, tempText, (int)wcslen(tempText));

            SetTextColor(hdc, health.semantic >= 0.95 ? RGB(0, 255, 120) : RGB(255, 100, 100));
            wchar_t semText[128];
            swprintf_s(semText, L"SEMANTIC: %.2f", health.semantic);
            TextOutW(hdc, 140, 50, semText, (int)wcslen(semText));

            SetTextColor(hdc, health.operational >= 0.95 ? RGB(0, 255, 120) : RGB(255, 100, 100));
            wchar_t operText[128];
            swprintf_s(operText, L"OPERATIONAL: %.2f", health.operational);
            TextOutW(hdc, 260, 50, operText, (int)wcslen(operText));

            SetTextColor(hdc, health.realization >= 0.95 ? RGB(0, 255, 120) : RGB(255, 100, 100));
            wchar_t realText[128];
            swprintf_s(realText, L"REALIZATION: %.2f", health.realization);
            TextOutW(hdc, 390, 50, realText, (int)wcslen(realText));

            // Recovery Velocity metrics
            SetTextColor(hdc, RGB(200, 200, 200));
            wchar_t velocityText[256];
            swprintf_s(velocityText, L"Repair Ticks: %.1f | Half-Life: %.1f | Quarantine Recovery: %.1f T",
                       health.meanDivergenceRepairTicks,
                       health.stabilizationHalfLife,
                       health.meanQuarantineRecoveryTime);
            TextOutW(hdc, 20, 80, velocityText, (int)wcslen(velocityText));

            // Long-horizon decay metrics
            wchar_t decayText1[256];
            swprintf_s(decayText1, L"Repair Freq Trend: %.4f | Rollover Backlog Slope: %.2f",
                       health.repairFrequencyTrend,
                       health.backlogTrendSlope);
            TextOutW(hdc, 20, 110, decayText1, (int)wcslen(decayText1));

            wchar_t decayText2[256];
            swprintf_s(decayText2, L"Quarantine Recur: %zu | Budget Pressure: %.1f%%",
                       health.quarantineRecurrence,
                       health.budgetPressure * 100.0);
            TextOutW(hdc, 20, 130, decayText2, (int)wcslen(decayText2));
            
            // Replay status (if replaying)
            if (scheduler->traceRecorder() && scheduler->traceRecorder()->isReplayMode()) {
                SetTextColor(hdc, RGB(255, 150, 0));
                wchar_t replayText[256];
                swprintf_s(replayText, L"REPLAY PLAYBACK MODE: Frame index %zu", scheduler->replayFrameIndex());
                TextOutW(hdc, 20, 160, replayText, (int)wcslen(replayText));
            } else {
                // General performance P99 indicators
                SetTextColor(hdc, RGB(0, 255, 255));
                auto cLatency = scheduler->commitLatencyTracker().compute();
                wchar_t perfText[256];
                swprintf_s(perfText, L"Commit P99: %.2fms | User/GDI Handles: %zu/%zu",
                           cLatency.p99, scheduler->metrics().userHandlesCount, scheduler->metrics().gdiHandlesCount);
                TextOutW(hdc, 20, 160, perfText, (int)wcslen(perfText));
            }

            // Expanded log (only if confidence is not High)
            if (health.confidence != KernelConfidence::High) {
                auto& events = scheduler->eventLog().events();
                if (!events.empty()) {
                    // Draw log background card
                    bgBrush = CreateSolidBrush(RGB(35, 15, 15));
                    borderPen = CreatePen(PS_SOLID, 1, RGB(180, 50, 50));
                    oldBgBrush = (HBRUSH)SelectObject(hdc, bgBrush);
                    oldBorderPen = (HPEN)SelectObject(hdc, borderPen);
                    RoundRect(hdc, 10, 240, 520, 320, 8, 8);
                    SelectObject(hdc, oldBgBrush);
                    SelectObject(hdc, oldBorderPen);
                    DeleteObject(bgBrush);
                    DeleteObject(borderPen);

                    SetTextColor(hdc, RGB(255, 100, 100));
                    wchar_t logHeader[] = L"RECENT WARNING EVENTS (CONDITIONAL AUDIT):";
                    TextOutW(hdc, 20, 245, logHeader, (int)wcslen(logHeader));

                    int logY = 265;
                    // Show last 2 events
                    size_t startIdx = events.size() > 2 ? events.size() - 2 : 0;
                    for (size_t i = startIdx; i < events.size(); ++i) {
                        auto& ev = events[i];
                        wchar_t logLine[512];
                        const char* typeStr = "Unknown";
                        switch (ev.type) {
                            case FailureEvent::Type::Divergence: typeStr = "DIVERGENCE"; break;
                            case FailureEvent::Type::Starvation: typeStr = "STARVATION"; break;
                            case FailureEvent::Type::Quarantine: typeStr = "QUARANTINE"; break;
                            case FailureEvent::Type::Rollback:   typeStr = "ROLLBACK"; break;
                            case FailureEvent::Type::EpochDesync: typeStr = "EPOCH_DESYNC"; break;
                            case FailureEvent::Type::ActivationDenial: typeStr = "ACTIVATION_DENIAL"; break;
                        }
                        swprintf_s(logLine, L"[%hs] Node #%u: %hs", typeStr, ev.surfaceId, ev.details.c_str());
                        TextOutW(hdc, 20, logY, logLine, (int)wcsnlen_s(logLine, 75));
                        logY += 20;
                    }
                }
            }
        }
    }
}

LRESULT CALLBACK DebugOverlay::DebugWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace morphic
