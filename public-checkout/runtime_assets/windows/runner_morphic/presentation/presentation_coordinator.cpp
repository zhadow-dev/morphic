#include "presentation/presentation_coordinator.h"

#include <cmath>
#include <string>
#include <vector>

#include "compositor/compositor_runtime.h"
#include "forensic_trace.h"
#include "surface_model.h"
#include "surface_shell.h"

namespace {

double ElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) {
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  if (freq.QuadPart == 0) return 0.0;
  return (end.QuadPart - start.QuadPart) * 1000.0 /
         static_cast<double>(freq.QuadPart);
}

long MaxAxisDiff(const RECT& a, const RECT& b) {
  return (std::max)({std::abs(static_cast<long>(a.left - b.left)),
                     std::abs(static_cast<long>(a.top - b.top)),
                     std::abs(static_cast<long>(a.right - b.right)),
                     std::abs(static_cast<long>(a.bottom - b.bottom))});
}

std::string IdOf(SurfaceShell* s) { return s ? s->id() : std::string("<none>"); }

}  // namespace

PresentationCoordinator::PresentationCoordinator(SurfaceModel* model,
                                                 FrameClock* clock)
    : model_(model), clock_(clock) {
  forensic::Log("PRESENT", "PresentationCoordinator created (settle-only)");
}

PresentationCoordinator::~PresentationCoordinator() {
  if (clock_ && clock_token_ != 0) {
    clock_->Unsubscribe(clock_token_);
    clock_token_ = 0;
  }
}

void PresentationCoordinator::RequestSettle(SurfaceShell* surface,
                                            const RECT& from) {
  if (!kPresentationEnabled) return;  // disabled → caller already hard-projected
  if (surface == nullptr || surface->GetHandle() == nullptr || model_ == nullptr) {
    return;
  }

  // Semantic truth is the surface's CURRENT model bounds (the final position the
  // interaction committed). If `from` already equals it, nothing to smooth.
  const RECT semantic = model_->bounds(surface);
  const long gap = MaxAxisDiff(from, semantic);
  if (gap <= kSettleSnapPx) {
    return;  // no residual worth smoothing
  }

  Settle s{};
  s.presented = from;
  QueryPerformanceCounter(&s.start_qpc);
  s.ticks = 0;
  settles_[surface] = s;

  ++metrics_.settle_count;
  if (gap > metrics_.peak_delta_px) metrics_.peak_delta_px = gap;

  forensic::Log("PRESENT", "settle begin id=" + IdOf(surface) +
                               " gap=" + std::to_string(gap) + "px");
  StartClockIfNeeded();
}

void PresentationCoordinator::CancelSettle(SurfaceShell* surface) {
  if (settles_.erase(surface) > 0) {
    forensic::Log("PRESENT", "settle cancel id=" + IdOf(surface));
    StopClockIfIdle();
  }
}

RECT PresentationCoordinator::presented(SurfaceShell* surface) const {
  auto it = settles_.find(surface);
  if (it != settles_.end()) return it->second.presented;
  return model_ ? model_->bounds(surface) : RECT{};
}

void PresentationCoordinator::OnTick(double dt_ms) {
  if (settles_.empty()) return;

  // alpha for a critically-damped approach. dt==0 (first tick) → no advance.
  const double alpha =
      (dt_ms > 0.0) ? (1.0 - std::exp(-dt_ms / kSettleTauMs)) : 0.0;

  // Collect completed surfaces to erase after iterating (don't mutate mid-loop).
  std::vector<SurfaceShell*> done;

  for (auto& [surface, s] : settles_) {
    if (surface == nullptr || surface->GetHandle() == nullptr) {
      done.push_back(surface);
      continue;
    }
    // Read semantic FRESH each tick: if truth changed mid-settle, we chase the NEW
    // truth (I-E3 — semantic always wins; presentation never fights it).
    const RECT semantic = model_->bounds(surface);

    auto blend = [alpha](LONG p, LONG t) -> LONG {
      return static_cast<LONG>(std::lround(
          static_cast<double>(p) +
          (static_cast<double>(t) - static_cast<double>(p)) * alpha));
    };
    RECT next{
        blend(s.presented.left, semantic.left),
        blend(s.presented.top, semantic.top),
        blend(s.presented.right, semantic.right),
        blend(s.presented.bottom, semantic.bottom),
    };

    ++s.ticks;
    const long gap = MaxAxisDiff(next, semantic);
    if (gap > metrics_.peak_delta_px) metrics_.peak_delta_px = gap;

    // Complete: within snap distance, OR watchdog (I-E4 — every settle terminates).
    const bool snap_done = gap <= kSettleSnapPx;
    const bool watchdog = s.ticks >= kMaxSettleTicks;
    if (snap_done || watchdog) {
      next = semantic;  // land exactly on truth
      ProjectNative(surface, next);  // NATIVE projection only — model.bounds untouched
      s.presented = next;
      LARGE_INTEGER now_qpc{};
      QueryPerformanceCounter(&now_qpc);
      const double ms = ElapsedMs(s.start_qpc, now_qpc);
      if (ms > metrics_.peak_settle_ms) metrics_.peak_settle_ms = ms;
      ++metrics_.settle_completed;
      if (watchdog && !snap_done) {
        ++metrics_.settle_forced;
        forensic::Log("PRESENT WARN", "settle FORCED (watchdog) id=" +
                                          IdOf(surface) + " ticks=" +
                                          std::to_string(s.ticks));
      } else {
        forensic::Log("PRESENT", "settle done id=" + IdOf(surface) +
                                     " ticks=" + std::to_string(s.ticks) +
                                     " ms=" + std::to_string(static_cast<int>(ms)));
      }
      done.push_back(surface);
      continue;
    }

    // Mid-settle: project the eased presented onto the native HWND ONLY. This does
    // NOT call SurfaceModel::SetBounds — semantic truth stays the final position;
    // only the native projection lags behind it for these few ticks.
    ProjectNative(surface, next);
    s.presented = next;
  }

  for (SurfaceShell* surface : done) settles_.erase(surface);
  StopClockIfIdle();
}

void PresentationCoordinator::StartClockIfNeeded() {
  if (clock_ == nullptr || clock_token_ != 0) return;
  clock_token_ = clock_->Subscribe([this](double dt_ms) { OnTick(dt_ms); });
}

void PresentationCoordinator::StopClockIfIdle() {
  if (settles_.empty() && clock_ && clock_token_ != 0) {
    clock_->Unsubscribe(clock_token_);
    clock_token_ = 0;
  }
}

void PresentationCoordinator::ProjectNative(SurfaceShell* surface,
                                            const RECT& rect) {
  if (compositor_) {
    compositor_->Project(surface, rect);
  } else if (surface && surface->GetHandle()) {
    surface->ApplyGeometry(rect);
  }
}
