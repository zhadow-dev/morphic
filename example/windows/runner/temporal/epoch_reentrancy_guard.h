#ifndef RUNNER_TEMPORAL_EPOCH_REENTRANCY_GUARD_H_
#define RUNNER_TEMPORAL_EPOCH_REENTRANCY_GUARD_H_

#include <array>

// PHASE 8B.7 — EpochReentrancyGuard.
//
// When the FrameScheduler owns the outer loop, it inherits responsibility for
// nested pumps: a modal dialog, a shell context menu, an IME composition window,
// or a blocking COM call can spin a SECONDARY message loop INSIDE our
// DispatchMessage. While that nested pump runs, OUR epoch is suspended — the
// scheduler is not pacing anything; something else owns the thread.
//
// This guard DETECTS that; it does NOT try to drive or prevent it (you cannot —
// the OS owns those loops). Two outputs matter:
//   1. current depth — so the active FrameEpoch can be flagged NestedPump/ModalLoop
//      and carry the depth it reached.
//   2. a depth HISTOGRAM — because an isolated depth-2 pump (open one menu) is
//      NORMAL, whereas CASCADING depth (depth climbing 3, 4, 5...) is the
//      catastrophic degradation pattern. Only the distribution reveals that;
//      a single max value hides it.
//
// Usage: construct a Scope at the top of each epoch body. Its ctor increments
// depth and records the new depth into the histogram; its dtor decrements. If a
// nested pump runs, a NEW Scope (from the re-entered epoch body, or simply the
// depth observed via depth()) reflects the nesting.
class EpochReentrancyGuard {
 public:
  // Histogram buckets for observed nesting depth. depth 1 is the normal,
  // non-nested epoch; depth >= 2 means a pump ran inside a pump.
  struct DepthHistogram {
    int depth1 = 0;     // normal, no nesting
    int depth2 = 0;     // single nested pump (menu, dialog) — usually benign
    int depth3 = 0;     // nested-in-nested — watch
    int depth4plus = 0; // cascading — pathological
    int total = 0;
    int peak_depth = 0;

    void record(int depth) {
      ++total;
      if (depth > peak_depth) peak_depth = depth;
      if (depth <= 1) ++depth1;
      else if (depth == 2) ++depth2;
      else if (depth == 3) ++depth3;
      else ++depth4plus;
    }
  };

  // RAII scope. One per epoch-body entry.
  class Scope {
   public:
    explicit Scope(EpochReentrancyGuard& guard) : guard_(guard) {
      depth_ = guard_.Enter();
    }
    ~Scope() { guard_.Leave(); }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    // Depth observed at the moment this scope was entered (1 == not nested).
    int depth() const { return depth_; }
    bool nested() const { return depth_ > 1; }

   private:
    EpochReentrancyGuard& guard_;
    int depth_ = 0;
  };

  int depth() const { return depth_; }
  const DepthHistogram& histogram() const { return histogram_; }
  void reset() { histogram_ = {}; }  // depth_ is left alone (live counter)

 private:
  // Returns the depth AFTER entering (so first entry returns 1).
  int Enter() {
    ++depth_;
    histogram_.record(depth_);
    return depth_;
  }
  void Leave() {
    if (depth_ > 0) --depth_;
  }

  int depth_ = 0;
  DepthHistogram histogram_;
};

#endif  // RUNNER_TEMPORAL_EPOCH_REENTRANCY_GUARD_H_
