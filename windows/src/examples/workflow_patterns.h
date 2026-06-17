#pragma once

#include "../../include/morphic/morphic_api.h"
#include <string>
#include <vector>

namespace morphic {
namespace examples {

// Phase 8B: Real Application Integration Patterns.
//
// These are NOT toy demos. They model real operational pressure:
// async streams, renderer churn, interruption, persistence reload,
// workspace switching mid-operation, long-lived sessions.
//
// Each pattern exercises specific kernel subsystems through
// the PUBLIC API ONLY — no kernel internals.

// ============================================================================
// Pattern 1: IDE Workflow
// ============================================================================
// The hardest workflow. Exercises everything:
// - Multiple workspace intents (Editing, Debugging, Inspecting, Reference)
// - Workflow associations (editor <-> logs <-> inspector <-> metrics)
// - Attention economics (active editor, passive logs, urgent diagnostics)
// - Session continuity (crash mid-debug, restore workflow context)
// - Interruption semantics (quick search -> return to debugging)
//
// Surfaces:
//   editor     — ContinuityCritical, Active
//   logs       — Persistent, PassiveMonitoring
//   inspector  — Persistent, LatentContinuity
//   metrics    — BackgroundDominant, Background
//   search     — Transient, Interruptible
//   reference  — Persistent, Background
//   console    — Persistent, PassiveMonitoring (detached)

struct IDEWorkflowPattern {
    static api::WorkspaceConfig editorWorkspace() {
        return {api::Activity::Editing, api::Disposition::ContinuityCritical,
                "Main Editor"};
    }

    static api::WorkspaceConfig debugWorkspace() {
        return {api::Activity::Debugging, api::Disposition::InterruptSensitive,
                "Debug Session"};
    }

    static api::WorkspaceConfig referenceWorkspace() {
        return {api::Activity::Reference, api::Disposition::BackgroundDominant,
                "API Docs"};
    }

    static api::WorkspaceConfig searchWorkspace() {
        return {api::Activity::Searching, api::Disposition::Transient,
                "Find in Files"};
    }

    static void setupWorkflow(api::MorphicRuntime& runtime,
                               api::SurfaceHandle editor,
                               api::SurfaceHandle logs,
                               api::SurfaceHandle inspector,
                               api::SurfaceHandle metrics) {
        runtime.associateSurfaces(editor, logs, api::Association::Monitoring);
        runtime.associateSurfaces(editor, inspector, api::Association::Inspecting);
        runtime.associateSurfaces(logs, metrics, api::Association::Monitoring);

        runtime.declareSurfaceAttention(editor, api::Attention::Active);
        runtime.declareSurfaceAttention(logs, api::Attention::PassiveMonitoring);
        runtime.declareSurfaceAttention(inspector, api::Attention::LatentContinuity);
        runtime.declareSurfaceAttention(metrics, api::Attention::Background);
    }
};

// ============================================================================
// Pattern 2: Monitoring Dashboard
// ============================================================================

struct MonitoringDashboardPattern {
    static api::WorkspaceConfig dashboardWorkspace() {
        return {api::Activity::Monitoring, api::Disposition::BackgroundDominant,
                "System Dashboard"};
    }

    static void setupWorkflow(api::MorphicRuntime& runtime,
                               api::SurfaceHandle cpuChart,
                               api::SurfaceHandle memChart,
                               api::SurfaceHandle logTail,
                               api::SurfaceHandle alertFeed) {
        runtime.associateSurfaces(cpuChart, memChart, api::Association::SharedContext);
        runtime.associateSurfaces(logTail, alertFeed, api::Association::Monitoring);

        runtime.declareSurfaceAttention(cpuChart, api::Attention::PassiveMonitoring);
        runtime.declareSurfaceAttention(memChart, api::Attention::PassiveMonitoring);
        runtime.declareSurfaceAttention(logTail, api::Attention::PassiveMonitoring);
        runtime.declareSurfaceAttention(alertFeed, api::Attention::Urgent);
    }
};

// ============================================================================
// Pattern 3: Debugging Workflow
// ============================================================================

struct DebuggingWorkflowPattern {
    static api::WorkspaceConfig debugWorkspace() {
        return {api::Activity::Debugging, api::Disposition::InterruptSensitive,
                "Debug: Auth Flow"};
    }

    static void setupWorkflow(api::MorphicRuntime& runtime,
                               api::SurfaceHandle editor,
                               api::SurfaceHandle logs,
                               api::SurfaceHandle inspector,
                               api::SurfaceHandle console) {
        runtime.associateSurfaces(editor, logs, api::Association::Monitoring);
        runtime.associateSurfaces(editor, inspector, api::Association::Inspecting);
        runtime.associateSurfaces(logs, console, api::Association::SharedContext);

        runtime.declareSurfaceAttention(editor, api::Attention::Active);
        runtime.declareSurfaceAttention(logs, api::Attention::PassiveMonitoring);
        runtime.declareSurfaceAttention(inspector, api::Attention::Active);
        runtime.declareSurfaceAttention(console, api::Attention::Interruptible);
    }
};

// ============================================================================
// Pattern 4: Multi-Document Reference
// ============================================================================

struct ReferenceWorkflowPattern {
    static api::WorkspaceConfig compareWorkspace() {
        return {api::Activity::Comparing, api::Disposition::Persistent,
                "API Comparison"};
    }

    static void setupWorkflow(api::MorphicRuntime& runtime,
                               api::SurfaceHandle docA,
                               api::SurfaceHandle docB,
                               api::SurfaceHandle lookup) {
        runtime.associateSurfaces(docA, docB, api::Association::CoEditing);
        runtime.associateSurfaces(docA, lookup, api::Association::TemporaryCompanion);

        runtime.declareSurfaceAttention(docA, api::Attention::Active);
        runtime.declareSurfaceAttention(docB, api::Attention::Active);
        runtime.declareSurfaceAttention(lookup, api::Attention::Interruptible);
    }
};

} // namespace examples
} // namespace morphic
