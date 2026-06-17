#pragma once

namespace morphic {

// Phase 4 — Runtime lifecycle state machine.
//
// Every subsystem that has phase-dependent behavior MUST check this.
//
// Prevents:
//   - creating surfaces during shutdown
//   - starting orchestration during bootstrap
//   - accepting method calls before initialization
//   - wake-after-destroy
//   - orphan recovery timers
//   - delayed event dispatch into dead systems
//
// Transitions are one-directional except Suspended ↔ Running.
//
//   Bootstrap → Initializing → Running ↔ Suspended → ShuttingDown → Destroyed
//
// THREAD: UI thread only. Phase transitions are NOT thread-safe.
enum class RuntimePhase {
    Bootstrap,      // Process started, nothing initialized.
                    // Only basic setup (COM, console) has occurred.

    Initializing,   // Subsystems being created, not yet operational.
                    // HWND creation, engine bootstrap, compositor init.
                    // Method channel calls are NOT accepted yet.

    Running,        // Fully operational, accepting commands.
                    // All subsystems initialized, event bus active.

    Suspended,      // App minimized/backgrounded, schedulers throttled.
                    // Orchestration paused, renderers may be parked.
                    // Transitions back to Running on restore.

    ShuttingDown,   // Teardown in progress, no new operations accepted.
                    // Method calls return errors.
                    // Destruction chain executing.

    Destroyed       // Everything cleaned up, process exiting.
                    // No pointers are valid. No calls are safe.
};

inline const char* toString(RuntimePhase phase) {
    switch (phase) {
        case RuntimePhase::Bootstrap:     return "bootstrap";
        case RuntimePhase::Initializing:  return "initializing";
        case RuntimePhase::Running:       return "running";
        case RuntimePhase::Suspended:     return "suspended";
        case RuntimePhase::ShuttingDown:  return "shuttingDown";
        case RuntimePhase::Destroyed:     return "destroyed";
    }
    return "unknown";
}

// Runtime phase guard — subsystems use this to reject operations
// during inappropriate phases.
inline bool isOperational(RuntimePhase phase) {
    return phase == RuntimePhase::Running || phase == RuntimePhase::Suspended;
}

inline bool acceptsNewWork(RuntimePhase phase) {
    return phase == RuntimePhase::Running;
}

}  // namespace morphic
