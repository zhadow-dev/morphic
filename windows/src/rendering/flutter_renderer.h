#pragma once

#include "renderer_surface.h"
#include "../debug/frame_cadence_monitor.h"
#include <flutter_windows.h>
#include <flutter/standard_method_codec.h>
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <chrono>

namespace morphic {

// Phase 2A.1 — Flutter renderer integration.
//
// Wraps the Flutter desktop embedder API (FlutterDesktopEngineCreate level).
// Morphic owns the HWND. Flutter creates a child view HWND for rendering.
//
// Lifecycle:
//   1. Morphic creates parent HWND (WindowHost)
//   2. FlutterRenderer::create() → FlutterDesktopEngineCreate + ViewControllerCreate
//   3. Flutter's view child HWND is parented into the Morphic HWND (inset by resize border)
//   4. Input routed via handleMessage → FlutterDesktopViewControllerHandleTopLevelWindowProc
//   5. destroy() → engine shutdown BEFORE HWND destruction
//
// CRITICAL: Morphic scheduler remains authoritative.
// Flutter rendering adapts to Morphic timing. NOT the reverse.
class FlutterRenderer : public RendererSurface {
public:
    struct EngineConfig {
        std::wstring assetsPath;
        std::wstring icuDataPath;
        std::wstring aotLibraryPath;    // Empty for debug builds
        std::string dartEntrypoint;     // Empty = "main"
    };

    // Inset constants — Flutter HWND must leave these zones exposed for Morphic
    static constexpr int kResizeBorder = 6;
    static constexpr int kDragHandleHeight = 32;  // Must match WindowHost::kDragHandleHeight

    explicit FlutterRenderer(const EngineConfig& config)
        : config_(config) {}

    ~FlutterRenderer() override {
        isAlive_ = false;
        if (isCreated()) destroy();
    }

    bool create(HWND parentHwnd, int width, int height) override {
        if (controller_) return true;  // Already created

        auto startTime = std::chrono::high_resolution_clock::now();

        parentHwnd_ = parentHwnd;

        // Step 1: Create engine
        FlutterDesktopEngineProperties props = {};
        props.assets_path = config_.assetsPath.c_str();
        props.icu_data_path = config_.icuDataPath.c_str();
        props.aot_library_path = config_.aotLibraryPath.empty()
            ? nullptr : config_.aotLibraryPath.c_str();
        props.dart_entrypoint = config_.dartEntrypoint.empty()
            ? nullptr : config_.dartEntrypoint.c_str();
        props.dart_entrypoint_argc = 0;
        props.dart_entrypoint_argv = nullptr;

        engine_ = FlutterDesktopEngineCreate(&props);
        if (!engine_) {
            OutputDebugStringA("FLUTTER_RENDERER: Engine create FAILED\n");
            return false;
        }

        // NOTE: Do NOT call FlutterDesktopEngineRun here.
        // ViewControllerCreate will auto-start the engine.

        // Step 2: Compute inset size (leave drag handle + resize border exposed)
        int insetW = width - 2 * kResizeBorder;
        int insetH = height - kDragHandleHeight - kResizeBorder;  // Top=drag, bottom=resize
        if (insetW < 100) insetW = 100;
        if (insetH < 100) insetH = 100;

        // Phase 2A.5: Register handler for secondary engine method channel.
        // MUST BE DONE BEFORE ViewControllerCreate so Dart doesn't miss it.
        auto* messenger = FlutterDesktopEngineGetMessenger(engine_);
        if (messenger) {
            FlutterDesktopMessengerSetCallback(
                messenger, "morphic",
                &FlutterRenderer::secondaryChannelCallback,
                this);
        }

        // Step 3: Create view controller (creates Flutter's child HWND + starts engine)
        controller_ = FlutterDesktopViewControllerCreate(insetW, insetH, engine_);
        if (!controller_) {
            OutputDebugStringA("FLUTTER_RENDERER: ViewController create FAILED\n");
            FlutterDesktopEngineDestroy(engine_);
            engine_ = nullptr;
            return false;
        }

        // Step 4: Get the Flutter view's HWND
        FlutterDesktopViewRef view = FlutterDesktopViewControllerGetView(controller_);
        if (!view) {
            OutputDebugStringA("FLUTTER_RENDERER: View is null\n");
            destroy();
            return false;
        }

        flutterHwnd_ = FlutterDesktopViewGetHWND(view);

        // Step 5: Parent Flutter's HWND into the Morphic surface HWND
        if (flutterHwnd_) {
            // Save the currently focused window so we can restore it after
            // showing the Flutter child — ViewControllerCreate + ShowWindow
            // will steal focus from the main Flutter engine, freezing the
            // entire app until Alt+Tab forces a focus change.
            HWND prevFocus = GetFocus();

            // Make it a child window style
            LONG style = GetWindowLong(flutterHwnd_, GWL_STYLE);
            style = (style & ~WS_POPUP) | WS_CHILD;
            SetWindowLong(flutterHwnd_, GWL_STYLE, style);

            SetParent(flutterHwnd_, parentHwnd_);

            // Inset: top = drag handle, sides/bottom = resize border
            RECT cr;
            GetClientRect(parentHwnd_, &cr);
            int cw = cr.right - 2 * kResizeBorder;
            int ch = cr.bottom - kDragHandleHeight - kResizeBorder;
            if (cw < 100) cw = 100;
            if (ch < 100) ch = 100;
            MoveWindow(flutterHwnd_, kResizeBorder, kDragHandleHeight, cw, ch, TRUE);

            // SW_SHOWNOACTIVATE: show without stealing focus/activation
            ShowWindow(flutterHwnd_, SW_SHOWNOACTIVATE);

            // Restore focus to whoever had it before engine creation
            if (prevFocus) SetFocus(prevFocus);
        }

        // Track first-frame callback for startup metrics
        FlutterDesktopEngineSetNextFrameCallback(engine_,
            [](void* userData) {
                auto* self = static_cast<FlutterRenderer*>(userData);
                self->metrics_.isRendering = true;
                OutputDebugStringA("FLUTTER_RENDERER: First frame rendered\n");
            }, this);

        FlutterDesktopViewControllerForceRedraw(controller_);

        auto endTime = std::chrono::high_resolution_clock::now();
        metrics_.startupMs = std::chrono::duration<double, std::milli>(
            endTime - startTime).count();

        OutputDebugStringA(("FLUTTER_RENDERER: Created in " +
            std::to_string(metrics_.startupMs) + "ms\n").c_str());

        return true;
    }

    void destroy() override {
        isAlive_ = false;
        epoch_++;
        auto startTime = std::chrono::high_resolution_clock::now();

        metrics_.isRendering = false;

        // CRITICAL: Destroy order is view controller → engine
        // The view controller owns the engine after ViewControllerCreate,
        // so destroying the controller also destroys the engine.
        //
        // WARNING: Destroying a secondary engine while primary is running
        // may crash. Use hideWithoutDestroy() for safe detach instead.
        if (controller_) {
            OutputDebugStringA("FLUTTER_RENDERER: Destroying ViewController...\n");
            FlutterDesktopViewControllerDestroy(controller_);
            controller_ = nullptr;
            engine_ = nullptr;  // Owned by controller
        } else if (engine_) {
            OutputDebugStringA("FLUTTER_RENDERER: Destroying Engine...\n");
            FlutterDesktopEngineDestroy(engine_);
            engine_ = nullptr;
        }

        flutterHwnd_ = nullptr;
        parentHwnd_ = nullptr;

        auto endTime = std::chrono::high_resolution_clock::now();
        metrics_.shutdownMs = std::chrono::duration<double, std::milli>(
            endTime - startTime).count();

        OutputDebugStringA(("FLUTTER_RENDERER: Destroyed in " +
            std::to_string(metrics_.shutdownMs) + "ms\n").c_str());
    }

    // Release presentation: detach Flutter engine from its surface HWND.
    //
    // IMPORTANT: This does NOT destroy the Flutter engine.
    // The engine's threads, Dart VM, Skia resources, and GPU state remain
    // alive as a RUNTIME-RESIDENT orphan until process exit.
    //
    // What this does:
    //   1. Hides the Flutter child HWND (stays as child for cascade destroy)
    //   2. Nulls controller_/engine_ pointers to DISARM the destructor
    //      (FlutterDesktopViewControllerDestroy crashes secondary engines)
    //   3. Marks isAlive_=false to block stale callbacks
    //
    // What this does NOT do:
    //   - Does NOT destroy the Flutter engine
    //   - Does NOT release engine threads
    //   - Does NOT free Dart VM state
    //   - Does NOT free GPU resources
    //
    // The engine becomes a "resident" — runtime memory cost without
    // presentation. This is the architectural reality of Flutter's multi-
    // engine model: secondary engines cannot be safely destroyed while
    // the primary is running.
    void releasePresentation() {
        if (flutterHwnd_) {
            ShowWindow(flutterHwnd_, SW_HIDE);
            OutputDebugStringA("FLUTTER_RENDERER: Presentation released (engine resident)\n");
        }
        isAlive_ = false;
        metrics_.isRendering = false;

        // Disarm destructor: null references to prevent
        // ~FlutterRenderer() from calling FlutterDesktopViewControllerDestroy().
        // The engine stays alive — we're amputating our handles, not destroying.
        controller_ = nullptr;
        engine_ = nullptr;
        flutterHwnd_ = nullptr;
        parentHwnd_ = nullptr;
    }

    // Legacy name — forwards to releasePresentation() for compatibility
    void hideWithoutDestroy() { releasePresentation(); }

    bool isCreated() const override { return controller_ != nullptr; }

    void resize(int width, int height) override {
        if (!flutterHwnd_ || !parentHwnd_) return;

        auto startTime = std::chrono::high_resolution_clock::now();

        // Inset: top = drag handle, sides/bottom = resize border
        RECT cr;
        GetClientRect(parentHwnd_, &cr);
        int cw = cr.right - 2 * kResizeBorder;
        int ch = cr.bottom - kDragHandleHeight - kResizeBorder;
        if (cw < 100) cw = 100;
        if (ch < 100) ch = 100;
        MoveWindow(flutterHwnd_, kResizeBorder, kDragHandleHeight, cw, ch, TRUE);

        if (controller_) {
            FlutterDesktopViewControllerForceRedraw(controller_);
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        metrics_.lastResizeCostMs = std::chrono::duration<double, std::milli>(
            endTime - startTime).count();
    }

    void forceRedraw() override {
        if (controller_) {
            FlutterDesktopViewControllerForceRedraw(controller_);
        }
    }

    bool handleMessage(HWND hwnd, UINT message, WPARAM wparam,
                       LPARAM lparam, LRESULT* result) override {
        if (!controller_ || !isAlive_) return false;

        // For mouse messages, only forward to Flutter when the cursor is
        // inside the Flutter child HWND bounds. Clicks in the drag handle
        // (top 32px) and resize borders (6px edges) must pass through to
        // Morphic's onMouseDown/onMouseMove/onMouseUp for drag/resize.
        if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && flutterHwnd_) {
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            RECT childRect;
            GetWindowRect(flutterHwnd_, &childRect);
            // Convert child rect to parent client coords
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&childRect), 2);
            if (!PtInRect(&childRect, pt)) {
                // Mouse is in the drag handle or resize border — let Morphic handle it
                return false;
            }
        }

        bool handled = FlutterDesktopViewControllerHandleTopLevelWindowProc(
            controller_, hwnd, message, wparam, lparam, result);

        if (handled) {
            metrics_.totalFramesRendered++;
        }

        if (message == WM_FONTCHANGE && engine_) {
            FlutterDesktopEngineReloadSystemFonts(engine_);
        }

        return handled;
    }

    Type type() const override { return Type::Flutter; }
    const char* typeName() const override { return "FlutterRenderer"; }
    const RenderMetrics& metrics() const override { return metrics_; }
    HWND childHwnd() const override { return flutterHwnd_; }

    // Phase 2B: Flutter capability declaration.
    // Based on empirical Phase 2A.5 evidence:
    //   - Animation pause: YES (AnimationController responds)
    //   - Cadence throttle: YES (can reduce frame reporting frequency)
    //   - Presentation freeze: UNKNOWN (needs AppLifecycleState experiment)
    //   - Visibility hints: PARTIAL (no native occlusion awareness)
    //   - Warm parking: UNKNOWN (needs experiment — may not fully suspend)
    //   - Cooperation: SemiCooperative (tickers pause but async/timer/isolate persist)
    RendererCapabilities capabilities() const override {
        return RendererCapabilities{
            true,   // supportsCadenceThrottle
            true,   // supportsAnimationPause
            false,  // supportsPresentationFreeze (unproven)
            false,  // supportsVisibilityHints (no native support)
            false,  // supportsWarmParking (unproven)
            CooperationLevel::SemiCooperative,
            std::chrono::milliseconds{0}  // wake latency unmeasured
        };
    }

    FlutterDesktopEngineRef engine() const { return engine_; }

    // Phase 2A.5: C-level handler for secondary engine "morphic" channel.
    // Handles frameProduced from Dart, sends success reply for all methods
    // to prevent MissingPluginException.
    static void secondaryChannelCallback(
        FlutterDesktopMessengerRef messenger,
        const FlutterDesktopMessage* message,
        void* user_data)
    {
        auto& codec = flutter::StandardMethodCodec::GetInstance();

        // Decode the method call
        auto methodCall = codec.DecodeMethodCall(
            message->message, message->message_size);

        if (methodCall && methodCall->method_name() == "frameProduced") {
            // Extract args and update FrameCadenceMonitor
            auto* args = std::get_if<flutter::EncodableMap>(
                methodCall->arguments());
            if (args) {
                int engineId = 0;
                double timestampMs = 0.0;

                auto eit = args->find(flutter::EncodableValue("engineId"));
                if (eit != args->end()) {
                    if (auto* iv = std::get_if<int32_t>(&eit->second))
                        engineId = *iv;
                    else if (auto* lv = std::get_if<int64_t>(&eit->second))
                        engineId = static_cast<int>(*lv);
                }

                auto tit = args->find(flutter::EncodableValue("timestampMs"));
                if (tit != args->end()) {
                    if (auto* dv = std::get_if<double>(&tit->second))
                        timestampMs = *dv;
                }

                FrameCadenceMonitor::instance().recordFrame(engineId, timestampMs);
            }
        }

        if (message->response_handle) {
            // Send success reply for ALL methods to prevent MissingPluginException
            flutter::EncodableValue successVal(true);
            auto response = codec.EncodeSuccessEnvelope(&successVal);
            FlutterDesktopMessengerSendResponse(
                messenger, message->response_handle,
                response->data(), response->size());
        }
    }

private:
    EngineConfig config_;
    HWND parentHwnd_ = nullptr;
    HWND flutterHwnd_ = nullptr;
    FlutterDesktopEngineRef engine_ = nullptr;
    FlutterDesktopViewControllerRef controller_ = nullptr;
    RenderMetrics metrics_;
    bool isAlive_ = true;          // Phase X: blocks stale callbacks
    uint32_t epoch_ = 0;           // Phase X: incremented on destroy

public:
    bool isAlive() const { return isAlive_; }
    uint32_t epoch() const { return epoch_; }
};

}  // namespace morphic
