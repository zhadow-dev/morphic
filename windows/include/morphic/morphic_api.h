#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// MORPHIC PUBLIC API — Consumer Boundary
// ============================================================================
//
// This is the ONLY header consumers should include.
// It is a FIREWALL — not a mirror of the kernel.
//
// Consumers see:  workspaces, intent, attention, associations, sessions, hints.
// Consumers NEVER see:  focus graph, transactions, topology, activation,
//                       continuity internals, pressure evaluation, governance.
//
// If you need to add something here, ask: "Does a real app need this?"
// If the answer involves kernel correctness, it does NOT belong here.
//
// THIS API IS INTENTIONALLY SMALL.
// Flexible APIs destroy semantic runtimes.
// ============================================================================

namespace morphic {
namespace api {

// --- Opaque Handles ---
// Consumers work with handles, never raw internal IDs.
struct WorkspaceHandle { uint64_t id = 0; bool valid() const { return id != 0; } };
struct SurfaceHandle   { uint64_t id = 0; bool valid() const { return id != 0; } };

// ============================================================================
// 1. WORKSPACE LIFECYCLE
// ============================================================================

// What is the workspace doing?
enum class Activity {
    Editing, Inspecting, Monitoring, Comparing,
    Reviewing, Searching, Debugging, Reference
};

// How should the workspace behave operationally?
enum class Disposition {
    Persistent, Transient, InterruptSensitive,
    ContinuityCritical, BackgroundDominant, Collaborative
};

struct WorkspaceConfig {
    Activity activity = Activity::Editing;
    Disposition disposition = Disposition::Persistent;
    std::string label;  // Human-readable name (e.g., "Auth Refactor")
};

// ============================================================================
// 2. ATTENTION
// ============================================================================

enum class Attention {
    Active, PassiveMonitoring, LatentContinuity,
    Interruptible, Urgent, Background
};

// ============================================================================
// 3. WORKFLOW ASSOCIATIONS
// ============================================================================
//
// CRITICAL DESIGN DECISION:
// Consumers may ONLY declare ASSOCIATIONS between surfaces.
// NOT dependencies. NOT restore ordering. NOT authority relationships.
//
// "These surfaces work together" is valid.
// "This surface depends on that one" is NOT valid through this API.
//
// Dependency semantics live inside the kernel (Phase 5).
// Consumers must NEVER recreate shadow authority systems.

enum class Association {
    CoEditing,          // Working on the same content
    Inspecting,         // One examines the other's state
    Monitoring,         // One watches the other's output
    TemporaryCompanion, // Short-lived grouping
    SharedContext       // Loosely related (includes derived/based-on)
};

// ============================================================================
// 4. SESSION
// ============================================================================

enum class InterruptionReason {
    IntentionalPause, ForcedSuspend, CrashRecovery,
    TemporaryDiversion, UrgentInterruption, SessionEnd
};

// ============================================================================
// 6. DIAGNOSTICS (read-only)
// ============================================================================

struct RuntimeHealth {
    int workspaceCount = 0;
    int surfaceCount = 0;
    int healthyRenderers = 0;
    int totalRenderers = 0;
    bool underPressure = false;
    bool degradedModeActive = false;
    std::string summary;
};

// ============================================================================
// MORPHIC RUNTIME INTERFACE
// ============================================================================
//
// The consumer-facing runtime. Wraps the kernel with a constrained API.
// Implementations live inside the kernel — consumers never instantiate.
//
// THIS API IS DELIBERATELY OPINIONATED.
// Consumers cannot: mutate focus, influence topology, control continuity,
// manipulate orchestration, or access governance internals.

class MorphicRuntime {
public:
    virtual ~MorphicRuntime() = default;

    // --- Workspace Lifecycle ---
    virtual WorkspaceHandle createWorkspace(const WorkspaceConfig& config) = 0;
    virtual void destroyWorkspace(WorkspaceHandle workspace) = 0;
    virtual void switchWorkspace(WorkspaceHandle workspace) = 0;
    virtual WorkspaceHandle activeWorkspace() const = 0;
    virtual void updateWorkspaceConfig(WorkspaceHandle workspace,
                                        const WorkspaceConfig& config) = 0;

    // --- Surface Attention (advisory) ---
    virtual void declareSurfaceAttention(SurfaceHandle surface,
                                          Attention level) = 0;

    // --- Workflow Associations (NOT dependencies) ---
    // "These surfaces work together." Nothing more.
    virtual void associateSurfaces(SurfaceHandle a, SurfaceHandle b,
                                    Association association) = 0;
    virtual void dissociateSurface(SurfaceHandle surface) = 0;

    // --- Session ---
    virtual void saveSession(InterruptionReason reason) = 0;
    virtual void restoreSession() = 0;

    // --- Telemetry & Privacy Boundaries ---
    virtual void setOptInProductivity(bool enabled) = 0;
    virtual bool isOptInProductivity() const = 0;

    // --- Diagnostics (read-only) ---
    virtual RuntimeHealth getHealth() const = 0;
};

} // namespace api
} // namespace morphic
