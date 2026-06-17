#pragma once

#include <windows.h>
#include <string>
#include <cassert>

namespace morphic {

// Phase 2A.2 — Thread Affinity Rules
//
// MORPHIC THREADING MODEL
// =======================
//
// Morphic is a SINGLE-THREADED runtime. All operations execute on the
// Win32 UI thread (the thread that owns the message pump).
//
// This is NOT a simplification — it's a deliberate architectural choice:
//   - All HWND operations (Create, Destroy, SetWindowPos, DeferWindowPos)
//     MUST execute on the thread that owns the HWND
//   - Flutter engine callbacks (via FlutterDesktopEngineCreate) arrive on
//     the platform thread, which IS the UI thread in the Flutter embedder
//   - The compositor frame loop runs via SetTimer/TimerProc on the UI thread
//   - Method channel calls from Dart arrive on the platform thread (= UI thread)
//
// THREAD OWNERSHIP RULES
// ======================
//
//   UI Thread (platform thread) owns:
//     - ALL HWND mutations (Create/Destroy/Move/Resize/Show/Hide)
//     - ALL DeferWindowPos batch operations
//     - Compositor frame pipeline (processFrame, all 8 stages)
//     - WindowHost WndProc message handling
//     - RendererManager state transitions
//     - Debug overlay painting
//     - Method channel handler execution
//     - Flutter engine lifecycle (create, view controller, hide)
//     - Metrics collection (write path)
//
//   NO other threads:
//     - There are no worker threads, no render threads, no scheduler threads
//     - Flutter engines may internally use their own raster threads, but
//       those never call back into Morphic directly
//     - If future phases require threading (e.g., async renderer pooling),
//       ALL Morphic-facing calls must be marshaled to the UI thread first
//
// FUTURE THREADING INVARIANTS
// ===========================
//
// If the system ever becomes multi-threaded:
//   1. HWND operations remain UI-thread-only (Win32 requirement)
//   2. SceneGraph mutations must be UI-thread-only OR guarded by lock
//   3. RendererManager state transitions must be atomic or UI-thread-only
//   4. Metrics reads may be cross-thread (const access), writes must be UI-thread
//   5. All new subsystems must declare their thread affinity via ThreadAffinity
//
// ENFORCEMENT
// ===========
//
// Use MORPHIC_ASSERT_UI_THREAD() at the entry point of any function that
// mutates compositor state, HWND state, or renderer lifecycle.
// This is a debug-mode assertion — zero cost in release builds.

class ThreadAffinity {
public:
    // Call once at startup (from the UI thread) to capture the thread ID.
    static void initialize() {
        uiThreadId_ = GetCurrentThreadId();
        initialized_ = true;
    }

    // Returns true if the caller is on the UI thread.
    static bool isUIThread() {
        return initialized_ && GetCurrentThreadId() == uiThreadId_;
    }

    // Returns the UI thread ID for debug metrics.
    static DWORD uiThreadId() { return uiThreadId_; }

    // Returns whether thread affinity has been initialized.
    static bool isInitialized() { return initialized_; }

    // Assert that we're on the UI thread. Debug-only.
    static void assertUIThread(const char* context) {
#ifndef NDEBUG
        if (initialized_ && GetCurrentThreadId() != uiThreadId_) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "THREAD_AFFINITY VIOLATION: %s called from thread %lu, "
                "expected UI thread %lu\n",
                context, GetCurrentThreadId(), uiThreadId_);
            OutputDebugStringA(buf);
            assert(false && "Thread affinity violation — see OutputDebugString");
        }
#endif
    }

private:
    static inline DWORD uiThreadId_ = 0;
    static inline bool initialized_ = false;
};

// Macro for convenient assertion at function entry.
// Zero cost in release builds.
#define MORPHIC_ASSERT_UI_THREAD() \
    ::morphic::ThreadAffinity::assertUIThread(__FUNCTION__)

// Phase 4 — Runtime thread domains.
//
// Formalizes which thread each subsystem executes on.
// Currently everything is UI thread. This enum exists so that
// future async scheduling can verify thread contracts.
//
// SUBSYSTEM AFFINITY TABLE
// ========================
//
//   Subsystem              | Thread     | Reason
//   -----------------------|------------|------------------------------------------
//   RuntimeKernel          | UI         | Owns message pump
//   SurfaceRegistry        | UI         | HWND mutations
//   TopologyManager        | UI         | SetWindowPos, SetParent, style mutation
//   ActivationManager      | UI         | DeferWindowPos, foreground management
//   Compositor             | UI         | Frame scheduling via SetTimer
//   RendererRuntime        | UI         | Engine create/destroy, messenger access
//   OrchestrationRuntime   | UI         | Governance drain via timer callback
//   FocusGraph             | UI         | Win32 focus API
//   InputRouter            | UI         | Message loop accelerator processing
//   WorkspaceController    | UI         | Workspace state
//   RuntimeEventBus        | UI         | Synchronous dispatch
//   RuntimeCapabilities    | ANY (read) | Immutable after initialization
//
// Flutter engine internals use their own raster/IO threads, but those
// NEVER call back into Morphic directly. All Morphic-facing callbacks
// arrive on the platform thread (= UI thread).
//
enum class RuntimeThread {
    UI,         // Win32 message pump thread. All HWND operations.
    Render,     // Future: dedicated render thread (not currently used).
    Worker,     // Future: background computation (not currently used).
    Any,        // Thread-safe. Only for immutable data (RuntimeCapabilities).
};

}  // namespace morphic
