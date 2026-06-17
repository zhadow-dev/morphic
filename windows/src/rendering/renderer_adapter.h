#pragma once

#include "renderer_capabilities.h"
#include "activity_state.h"

namespace morphic {

// Phase 2B — Renderer Adapter
//
// Renderer-specific command execution. Translates abstract orchestration
// commands into renderer-backend-specific actions.
//
// RENDERER CONTRACT BOUNDARY:
//   RendererAdapter MAY:
//     - send hints
//     - request cadence changes
//     - request parking
//     - report telemetry
//
//   RendererAdapter MAY NOT:
//     - mutate compositor topology
//     - own visibility
//     - alter scheduler authority
//     - bypass policy
//
// This is an abstract interface. FlutterRendererAdapter, GdiRendererAdapter
// are concrete implementations.
//
// THREAD: UI thread only.
class RendererAdapter {
public:
    virtual ~RendererAdapter() = default;

    // Request the renderer to transition to a new activity state.
    // Returns the result of the request — renderer may refuse.
    virtual CommandResult requestActivityTransition(ActivityState desired) = 0;

    // Query the renderer's current observable cadence.
    // Returns frames-per-second as observed, not as intended.
    virtual float observedCadence() const { return 0.0f; }

    // Query the renderer's declared capabilities.
    virtual RendererCapabilities capabilities() const = 0;
};

// Null adapter — accepts everything, does nothing.
// Used for NullRenderer and GdiRenderer (trivially cooperative).
class NullRendererAdapter : public RendererAdapter {
public:
    CommandResult requestActivityTransition(ActivityState) override {
        return CommandResult::Accepted;
    }

    RendererCapabilities capabilities() const override {
        return RendererCapabilities{
            true, true, true, true, true,
            CooperationLevel::Cooperative,
            std::chrono::milliseconds{0}
        };
    }
};

}  // namespace morphic
