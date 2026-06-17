#include "validation/activation_coherency_auditor.h"

#include <string>
#include <unordered_set>

#include "forensic_trace.h"
#include "surface_model.h"
#include "surface_shell.h"

namespace {
std::string IdOf(SurfaceShell* s) { return s ? s->id() : std::string("<none>"); }

// Is `hwnd` the owner-chain ancestor `owner` (or owner itself)? Generic Win32.
bool InOwnerChain(HWND hwnd, HWND owner) {
  for (HWND h = hwnd; h != nullptr; h = GetWindow(h, GW_OWNER)) {
    if (h == owner) return true;
  }
  return false;
}
}  // namespace

ActivationCoherencyAuditor::ActivationCoherencyAuditor(FrameClock* clock,
                                                       EventBus* bus,
                                                       SurfaceModel* model)
    : clock_(clock), bus_(bus), model_(model) {
  if (clock_) {
    token_ = clock_->Subscribe([this](double dt) { OnTick(dt); });
  }
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent e, SurfaceShell* s) { OnEvent(e, s); });
  }
  forensic::Log("COHERENCY", "ActivationCoherencyAuditor installed (log-only)");
}

ActivationCoherencyAuditor::~ActivationCoherencyAuditor() {
  if (clock_ && token_ != 0) clock_->Unsubscribe(token_);
  if (bus_ && bus_token_ != 0) bus_->Unsubscribe(bus_token_);
}

void ActivationCoherencyAuditor::OnTick(double /*dt_ms*/) {
  if (++tick_count_ % kAuditEveryNTicks != 0) return;
  Audit("periodic");
}

void ActivationCoherencyAuditor::OnEvent(RuntimeEvent event,
                                         SurfaceShell* /*surface*/) {
  // Audit right after an activation settles (z/foreground should now agree).
  if (event == RuntimeEvent::SurfaceActivated) Audit("activated");
}

void ActivationCoherencyAuditor::Audit(const char* reason) {
  if (model_ == nullptr) return;

  // --- [FOREGROUND DIVERGENCE] ---
  // The OS foreground window should be the semantic-active surface OR an owned
  // window of it (an owned palette/inspector legitimately holding foreground while
  // its workspace is the semantic active root). We only flag when foreground is one
  // of OUR surfaces but NOT coherent — a non-Morphic foreground (another app) is
  // not our concern and is skipped.
  SurfaceShell* active = model_->active();
  if (active != nullptr && active->GetHandle() != nullptr) {
    const HWND fg = GetForegroundWindow();
    const HWND active_h = active->GetHandle();
    // Is the foreground one of our surfaces?
    bool fg_is_ours = false;
    for (SurfaceShell* s : model_->z_order()) {
      if (s && s->GetHandle() == fg) { fg_is_ours = true; break; }
    }
    if (fg_is_ours && fg != active_h && !InOwnerChain(fg, active_h)) {
      forensic::Log("FOREGROUND DIVERGENCE",
                    std::string("reason=") + reason + " semantic_active=" +
                        IdOf(active) + " but foreground is a different surface");
    }
  }

  // --- [ZORDER FAIL] ---
  // The semantic top-z surface (z_order().front()) should be the highest of OUR
  // NON-TOPMOST surfaces in actual HWND z. (Topmost overlays live in a separate
  // band by design — they're excluded from this check.) Walk our surfaces and find
  // which non-topmost HWND the OS has highest, compare to the semantic front.
  const auto& z = model_->z_order();
  SurfaceShell* semantic_top = nullptr;
  for (SurfaceShell* s : z) {  // front()==index 0 is semantic top
    if (s && s->GetHandle() &&
        !(GetWindowLong(s->GetHandle(), GWL_EXSTYLE) & WS_EX_TOPMOST)) {
      semantic_top = s;
      break;
    }
  }
  if (semantic_top != nullptr) {
    // Build the set of our non-topmost HWNDs.
    std::unordered_set<HWND> ours;
    for (SurfaceShell* s : z) {
      if (s && s->GetHandle() &&
          !(GetWindowLong(s->GetHandle(), GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        ours.insert(s->GetHandle());
      }
    }
    // Walk the OS z-order top→bottom; the first of ours is the native top.
    HWND native_top = nullptr;
    for (HWND h = GetTopWindow(nullptr); h != nullptr;
         h = GetWindow(h, GW_HWNDNEXT)) {
      if (ours.count(h)) { native_top = h; break; }
    }
    // OWNERSHIP-AWARE expectation (PHASE 10.4): an OWNED window (owner != null) is
    // kept ABOVE its owner by the OS and is z-constrained relative to it, so the
    // semantic-top being owned does NOT mean it's the absolute native top — the OS
    // legitimately keeps the owner chain coherent. We only flag a TRUE divergence:
    // the native top and the semantic top are UNRELATED by ownership (neither owns
    // nor is owned by the other). Related-by-ownership = coherent, not a failure.
    if (native_top != nullptr && native_top != semantic_top->GetHandle()) {
      const HWND sem_h = semantic_top->GetHandle();
      const bool related = InOwnerChain(native_top, sem_h) ||
                           InOwnerChain(sem_h, native_top);
      if (!related) {
        forensic::Log("ZORDER FAIL",
                      std::string("reason=") + reason + " semantic_top=" +
                          IdOf(semantic_top) +
                          " but an UNRELATED surface is natively on top");
      }
    }
  }
}
