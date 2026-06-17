#include "src/interaction/input_router.h"
#include "src/composition/compositor.h" // For routing the final decision
#include "interaction_transaction.h"

namespace morphic {

InputRouter::InputRouter(Compositor* compositor) : compositor_(compositor) {}
InputRouter::~InputRouter() {}

bool InputRouter::routeKeyboardEvent(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) {
        return false;
    }

    std::optional<InteractionIntent> intent = decodeIntent(msg, wParam, lParam);
    if (!intent) {
        return false;
    }

    // 1. Semantic mutation sandbox (Transaction)
    InteractionTransaction tx(compositor_->focusGraph(), *compositor_);
    
    // 2. Stage the intent for evaluation
    tx.stageIntent(*intent, FocusInitiator::UserInput);
    
    // 3. Atomically commit the transaction (realization happens here safely)
    tx.commit();

    return true; // We consumed this shortcut
}

std::optional<InteractionIntent> InputRouter::decodeIntent(UINT msg, WPARAM wParam, LPARAM lParam) {
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    // Ctrl+Tab (CycleForward)
    if (ctrl && !shift && wParam == VK_TAB) {
        return InteractionIntent::CycleForward;
    }
    // Ctrl+Shift+Tab (CycleBackward)
    if (ctrl && shift && wParam == VK_TAB) {
        return InteractionIntent::CycleBackward;
    }
    // Esc (DismissOverlay/ExitModal)
    if (!ctrl && !shift && wParam == VK_ESCAPE) {
        return InteractionIntent::ExitModal; // Will be handled appropriately by FocusGraph
    }
    // Alt+` (ActivateDetached - arbitrary mapping for now)
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (alt && !ctrl && !shift && wParam == VK_OEM_3) { // VK_OEM_3 is usually backtick/tilde
        return InteractionIntent::ActivateDetached;
    }
    // Win+W or Ctrl+W (FocusWorkspace - arbitrary mapping for now)
    if (ctrl && !shift && wParam == 'W') {
        return InteractionIntent::FocusWorkspace;
    }

    return std::nullopt;
}

} // namespace morphic
