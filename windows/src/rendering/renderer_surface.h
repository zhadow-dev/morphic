#pragma once

#include "renderer_capabilities.h"
#include <windows.h>
#include <string>
#include <chrono>
#include <cstdint>

namespace morphic {

// Renderer ID — unique per renderer instance across the process lifetime.
using RenderId = uint32_t;
constexpr RenderId kInvalidRenderId = 0;

// Phase 2A — Renderer lifecycle state model.
//
// Separated into two orthogonal axes:
//   RendererLifecycle — engine alive/dead status (immutable facts)
//   RendererAttachment — topology relationship to surface (mutable binding)
//
// This separation allows expressing real states like:
//   Running + Detached   (engine alive, not bound to a surface)
//   Zombie + Hidden      (engine can't be destroyed, currently invisible)
//
// NOTE: This is intentionally richer than needed for Phase 2A.1.
// Future states (Suspended, Pooled) will be used for renderer migration
// and shared-engine pivot if multi-engine proves too expensive.

enum class RendererLifecycle {
    Uninitialized,  // Constructed but create() not yet called
    Initializing,   // create() in progress (engine spinning up)
    Running,        // Actively producing frames
    Suspended,      // Engine alive but frame production paused (future)
    Failed,         // create() or engine startup failed
    Zombie,         // Should be dead but can't be destroyed (Dart VM safety)
    Destroyed,      // Successfully destroyed (terminal state)
};

enum class RendererAttachment {
    Unattached,     // Not bound to any surface
    Attached,       // Actively bound to a surface HWND
    Hidden,         // hideWithoutDestroy() — alive but not visible
    Detached,       // Unparented from surface, may be reattached (future)
};

// Phase 2A — Renderer abstraction layer.
//
// Separates rendering from composition. The compositor controls surfaces;
// renderers paint content into them. This allows:
//   - GDI test surfaces (kernel validation without rendering complexity)
//   - Flutter rendering (Phase 2A target)
//   - NullRenderer (validation-only mode)
//   - Future: direct Skia if Flutter proves limiting
//
// CRITICAL: GDI test surfaces stay alive permanently.
// They are the ONLY way to isolate kernel bugs from rendering bugs.
//
// OWNERSHIP: RendererManager owns all renderer instances.
// WindowHost borrows a raw pointer to the active renderer.
// This is correct because renderer lifetime > surface lifetime.
class RendererSurface {
public:
    enum class Type {
        Null,       // No rendering — validation only
        Gdi,        // WM_PAINT/FillRect (existing Phase 1 surfaces)
        Flutter,    // Flutter engine rendering (Phase 2A)
    };

    struct RenderMetrics {
        double startupMs = 0.0;           // Time from create to first frame
        double shutdownMs = 0.0;          // Time from shutdown request to cleanup
        double lastFrameLatencyMs = 0.0;  // Last render frame latency
        double lastResizeCostMs = 0.0;    // Last resize duration
        double presentLatencyMs = 0.0;    // Last present latency
        double renderConvergenceMs = 0.0; // Position-converged to content-converged gap
        int64_t totalFramesRendered = 0;
        bool isRendering = false;

        // Per-engine frame tracking (Phase 2A.2)
        uint64_t lastProducedFrame = 0;      // Engine's own frame counter
        uint64_t lastPresentedFrame = 0;     // Last frame shown on screen
        uint64_t lastResizeFrame = 0;        // Frame when last resize completed
        uint64_t lastCompositionCommitFrame = 0;  // Last Morphic frame that moved this HWND

        // Visibility latency (Hidden -> first visible frame)
        double visibilityLatencyMs = 0.0;    // Time from show to first rendered frame
        std::chrono::high_resolution_clock::time_point showRequestTime;
        bool awaitingFirstVisibleFrame = false;
    };

    virtual ~RendererSurface() = default;

    // Lifecycle
    virtual bool create(HWND parentHwnd, int width, int height) = 0;
    virtual void destroy() = 0;
    virtual bool isCreated() const = 0;

    // Rendering
    virtual void resize(int width, int height) = 0;
    virtual void forceRedraw() = 0;

    // Input forwarding — returns true if handled
    virtual bool handleMessage(HWND hwnd, UINT message, WPARAM wparam,
                               LPARAM lparam, LRESULT* result) = 0;

    // Identity
    virtual Type type() const = 0;
    virtual const char* typeName() const = 0;

    // Metrics
    virtual const RenderMetrics& metrics() const = 0;

    // Phase 2B: Capability negotiation.
    // Renderers declare what orchestration they support.
    // Workload controller queries this instead of assuming.
    virtual RendererCapabilities capabilities() const {
        // Default: hostile, supports nothing.
        return RendererCapabilities{};
    }

    // The child HWND owned by this renderer (if any).
    // For GDI: returns null (renders directly into parent).
    // For Flutter: returns the Flutter view's child HWND.
    virtual HWND childHwnd() const { return nullptr; }

    // --- Lifecycle state (managed by RendererManager) ---

    RendererLifecycle lifecycle() const { return lifecycle_; }
    RendererAttachment attachment() const { return attachment_; }
    RenderId rendererId() const { return rendererId_; }

    void setLifecycle(RendererLifecycle state) { lifecycle_ = state; }
    void setAttachment(RendererAttachment state) { attachment_ = state; }
    void setRendererId(RenderId id) { rendererId_ = id; }

    // Convenience queries
    bool isAlive() const {
        return lifecycle_ == RendererLifecycle::Running ||
               lifecycle_ == RendererLifecycle::Suspended;
    }
    bool isZombie() const { return lifecycle_ == RendererLifecycle::Zombie; }
    bool isBound() const { return attachment_ == RendererAttachment::Attached; }

protected:
    RendererLifecycle lifecycle_ = RendererLifecycle::Uninitialized;
    RendererAttachment attachment_ = RendererAttachment::Unattached;
    RenderId rendererId_ = kInvalidRenderId;
};

// Null renderer — does nothing. Used for validation-only surfaces.
class NullRenderer : public RendererSurface {
public:
    bool create(HWND, int, int) override { created_ = true; return true; }
    void destroy() override { created_ = false; }
    bool isCreated() const override { return created_; }
    void resize(int, int) override {}
    void forceRedraw() override {}
    bool handleMessage(HWND, UINT, WPARAM, LPARAM, LRESULT*) override { return false; }
    Type type() const override { return Type::Null; }
    const char* typeName() const override { return "NullRenderer"; }
    const RenderMetrics& metrics() const override { return metrics_; }
    RendererCapabilities capabilities() const override {
        return RendererCapabilities{
            true, true, true, true, true,
            CooperationLevel::Cooperative,
            std::chrono::milliseconds{0}
        };
    }
private:
    bool created_ = false;
    RenderMetrics metrics_;
};

// GDI renderer — the existing WM_PAINT/FillRect path from Phase 1.
// This is the PERMANENT test renderer for kernel validation.
class GdiRenderer : public RendererSurface {
public:
    explicit GdiRenderer(COLORREF color) : color_(color) {}

    bool create(HWND parentHwnd, int width, int height) override {
        parentHwnd_ = parentHwnd;
        created_ = true;
        metrics_.isRendering = true;
        return true;
    }

    void destroy() override {
        created_ = false;
        metrics_.isRendering = false;
    }

    bool isCreated() const override { return created_; }

    void resize(int, int) override {
        // GDI doesn't need resize handling — WM_PAINT redraws at any size
    }

    void forceRedraw() override {
        if (parentHwnd_) InvalidateRect(parentHwnd_, nullptr, TRUE);
    }

    bool handleMessage(HWND hwnd, UINT message, WPARAM wparam,
                       LPARAM lparam, LRESULT* result) override {
        if (message == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HBRUSH brush = CreateSolidBrush(color_);
            FillRect(hdc, &ps.rcPaint, brush);
            DeleteObject(brush);
            EndPaint(hwnd, &ps);
            metrics_.totalFramesRendered++;
            *result = 0;
            return true;
        }
        return false;
    }

    Type type() const override { return Type::Gdi; }
    const char* typeName() const override { return "GdiRenderer"; }
    const RenderMetrics& metrics() const override { return metrics_; }
    RendererCapabilities capabilities() const override {
        return RendererCapabilities{
            true, true, true, true, true,
            CooperationLevel::Cooperative,
            std::chrono::milliseconds{0}
        };
    }

    void setColor(COLORREF c) { color_ = c; }

private:
    HWND parentHwnd_ = nullptr;
    COLORREF color_ = RGB(0x16, 0x21, 0x3e);
    bool created_ = false;
    RenderMetrics metrics_;
};

}  // namespace morphic
