#ifndef RUNNER_VALIDATION_DETERMINISTIC_REPLAY_H_
#define RUNNER_VALIDATION_DETERMINISTIC_REPLAY_H_

#include <windows.h>

#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

class InteractionRouter;
class SurfaceGraph;
class SurfaceModel;
class SurfaceShell;
class ForensicLogger;

// Phase 8B.6 — Deterministic Replay Infrastructure.
//
// Records interaction sessions precisely enough to replay them later.
// Replay drives through the REAL runtime via InteractionRouter —
// it does NOT bypass orchestration or directly mutate model state.
//
// Architecture:
//   ReplayScenario
//     → synthetic event injection
//     → InteractionRouter
//     → real runtime execution
//
// NOT:
//   ReplayScenario → direct SurfaceModel mutation
//
// This is a NEW system, NOT an extension of ReplaySystem or KernelTrace.
// Those belong to older orchestration eras. Merging would create
// semantic contamination, mixed abstraction levels, and legacy coupling.
//
// STORAGE: NDJSON (one JSON object per line) for determinism > compression.
// PATH:    validation_reports/<session>/replays/<scenario>.ndjson

// --- Replay Event Types ---
// Every event type that can affect runtime state during interaction.
enum class ReplayEventType {
  PointerDown,        // Pointer press at screen coords
  PointerMove,        // Pointer move (during drag/resize)
  PointerUp,          // Pointer release
  ResizeBegin,        // Native resize initiated
  ResizeUpdate,       // Resize geometry change
  ResizeEnd,          // Native resize complete
  Activate,           // Surface activation
  CaptureLost,        // External capture steal
  CaptureStolenBack,  // Capture reclaimed after external steal
  SurfaceCreated,     // New surface in topology
  SurfaceDestroyed,   // Surface removed from topology
  GroupCreated,        // Group formed
  GroupDestroyed,      // Group dissolved
  DpiChanged,         // DPI change event
  FocusCycle,         // Next/previous surface focus cycling
};

const char* ToString(ReplayEventType type);

// --- Replay Event ---
struct ReplayEvent {
  double relative_time_ms = 0.0;  // Relative to recording start
  ReplayEventType type = ReplayEventType::PointerDown;
  POINT screen_pt{};              // Screen coordinates (pointer events)
  int surface_index = 0;          // Index into surface list (deterministic)
  int resize_edge = 0;            // HT* code for resize events
  int w = 0, h = 0;               // Size for resize updates
  int dpi = 96;                   // DPI for DPI change events
  std::string metadata;           // Free-form annotation
};

// --- Replay Digest ---
//
// Semantic runtime state verification AFTER replay.
// This is what makes replay meaningful beyond "it didn't crash."
//
// Without a digest, replay verifies:
//   motion reproduction
//
// With a digest, replay verifies:
//   topology equivalence
//   z-order equivalence
//   session equivalence
//   transaction equivalence
//   interaction-state equivalence
//   projection equivalence
//   invariant parity
struct ReplayDigest {
  int surface_count = 0;
  int group_count = 0;
  int active_surface_index = -1;  // Index of active surface, -1 if none
  bool interaction_active = false;
  int transactions_open = 0;      // Pending deferred + transactional
  bool capture_held = false;
  std::vector<int> z_order;       // Surface indices in z-order (front to back)
  std::unordered_map<int, std::vector<int>> group_memberships;  // group_idx → member_indices

  bool operator==(const ReplayDigest& other) const {
    return surface_count == other.surface_count &&
           group_count == other.group_count &&
           active_surface_index == other.active_surface_index &&
           interaction_active == other.interaction_active &&
           transactions_open == other.transactions_open &&
           capture_held == other.capture_held &&
           z_order == other.z_order &&
           group_memberships == other.group_memberships;
  }

  bool operator!=(const ReplayDigest& other) const {
    return !(*this == other);
  }

  std::string diff(const ReplayDigest& other) const;
  std::string toJson() const;
};

// --- Replay Comparison Result ---
struct ReplayComparisonResult {
  bool identical = true;
  std::string divergence_summary;
  ReplayDigest recorded_digest;
  ReplayDigest replayed_digest;
};

// --- Deterministic Replay Capture ---
class DeterministicReplayCapture {
 public:
  // Callbacks for runtime interaction.
  // These inject synthetic events through the real runtime path.
  struct RuntimeCallbacks {
    // Interaction injection (through InteractionRouter)
    std::function<void(int surface_index, POINT pt)> beginDrag;
    std::function<void(int surface_index, POINT pt)> updateDrag;
    std::function<void(int surface_index, POINT pt)> endDrag;
    std::function<void(int surface_index, POINT pt, int edge)> beginResize;
    std::function<void(int surface_index, int w, int h)> updateResize;
    std::function<void(int surface_index)> endResize;
    std::function<void(int surface_index)> activate;
    std::function<void()> releaseCapture;
    std::function<void()> processFrame;
    std::function<void()> focusNext;
    std::function<void()> focusPrevious;

    // Topology operations
    std::function<void(int surface_index)> destroySurface;
    std::function<int(int w, int h)> createSurface;  // Returns new surface index
    std::function<int(std::vector<int> member_indices)> createGroup;
    std::function<void(int group_index)> destroyGroup;

    // Digest capture
    std::function<ReplayDigest()> captureDigest;
  };

  DeterministicReplayCapture() = default;

  // --- Recording ---
  void beginRecording();
  void recordEvent(const ReplayEvent& event);
  void endRecording();
  bool isRecording() const { return recording_; }
  size_t eventCount() const { return events_.size(); }

  // Convenience recorders
  void recordPointerDown(int surface_index, POINT pt);
  void recordPointerMove(int surface_index, POINT pt);
  void recordPointerUp(int surface_index, POINT pt);
  void recordActivation(int surface_index);
  void recordCaptureLost();
  void recordSurfaceCreated(int surface_index);
  void recordSurfaceDestroyed(int surface_index);
  void recordGroupCreated(int group_index, const std::vector<int>& members);
  void recordGroupDestroyed(int group_index);

  // --- Persistence ---
  bool saveToFile(const std::string& path) const;
  bool loadFromFile(const std::string& path);

  // --- Replay ---
  // Replays with real timing (respects original inter-event delays).
  ReplayComparisonResult replay(RuntimeCallbacks& cb);

  // Replays as fast as possible (for stress testing).
  ReplayComparisonResult replayImmediate(RuntimeCallbacks& cb);

  // --- Digest ---
  // Capture digest before recording starts (baseline) and after replay ends.
  void setRecordedDigest(const ReplayDigest& digest) { recordedDigest_ = digest; }
  const ReplayDigest& recordedDigest() const { return recordedDigest_; }

  const std::vector<ReplayEvent>& events() const { return events_; }

 private:
  void dispatchEvent(const ReplayEvent& event, RuntimeCallbacks& cb);

  std::vector<ReplayEvent> events_;
  bool recording_ = false;
  std::chrono::steady_clock::time_point recordStart_;
  ReplayDigest recordedDigest_;
};

#endif  // RUNNER_VALIDATION_DETERMINISTIC_REPLAY_H_
