#ifndef MORPHIC_SRC_INTERACTION_INPUT_ROUTER_H_
#define MORPHIC_SRC_INTERACTION_INPUT_ROUTER_H_

#include <windows.h>
#include <optional>
#include "src/interaction/focus_types.h"

namespace morphic {

class Compositor;

// -----------------------------------------------------------------------------
// Phase 4 Step 3C: Global Interaction Policy
//
// InputRouter intercepts physical keyboard/mouse input and decodes it into
// semantic InteractionIntent. It DOES NOT mutate HWNDs itself.
// -----------------------------------------------------------------------------
class InputRouter {
public:
    explicit InputRouter(Compositor* compositor);
    ~InputRouter();

    // Evaluates a raw Win32 keyboard event.
    // Returns true if the interaction was routed (consumed).
    bool routeKeyboardEvent(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    Compositor* compositor_;

    // Translates a physical key chord into semantic intent
    std::optional<InteractionIntent> decodeIntent(UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace morphic

#endif // MORPHIC_SRC_INTERACTION_INPUT_ROUTER_H_
