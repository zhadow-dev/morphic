#ifndef MORPHIC_SRC_INTERACTION_FOCUS_GRAPH_H_
#define MORPHIC_SRC_INTERACTION_FOCUS_GRAPH_H_

#include "focus_types.h"
#include <vector>
#include <unordered_map>
#include <optional>

namespace morphic {

// -----------------------------------------------------------------------------
// Phase 4 Step 3B: Semantic Truth Authority
//
// The FocusGraph is a pure semantic state database and arbitration engine.
// It DOES NOT mutate Win32 state. It DOES NOT call SetFocus().
// It evaluates "what should happen" and emits a FocusDecision.
// -----------------------------------------------------------------------------
class FocusGraph {
public:
    FocusGraph();
    ~FocusGraph();

    // --- Graph Mutation ---
    
    // Registers a surface into the semantic attention graph.
    // workspaceKey: which workspace chain to place it in (0 = use node's domain default)
    void registerNode(const FocusNode& node, uint64_t workspaceKey = 0);
    
    // Removes a surface from the graph and cleans up chains.
    void unregisterNode(NodeId id);
    
    // Updates the runtime eligibility of a node (e.g., Suspended, Hidden).
    void updateEligibility(NodeId id, FocusEligibility eligibility);

    // --- Phase 5B: Multi-Workspace ---
    
    // Sets the active workspace for navigation purposes.
    void setActiveWorkspace(uint64_t workspaceKey);
    uint64_t activeWorkspace() const { return activeWorkspaceKey_; }
    
    // Returns the chain for a specific workspace.
    const std::vector<NodeId>& workspaceChain(uint64_t workspaceKey) const;

    // --- Semantic Arbitration (The core logic) ---
    
    // Evaluates a user/runtime intent against the current semantic state
    // and returns a decision on what the new semantic focus should be.
    FocusDecision evaluateTransition(InteractionIntent intent, FocusInitiator initiator);

    // Evaluates restoration when the current focus is destroyed or yields.
    FocusDecision evaluateRestoration(FocusRestorePolicy policy);

    // Pushes a new modal capture policy, suppressing underlying layers.
    void pushModalSuppression(const ModalSuppressionPolicy& policy);
    
    // Pops the top modal capture policy and returns the required restoration decision.
    FocusDecision popModalSuppression(NodeId modalNodeId);

    // --- State Checkpoints ---
    
    // Captures the live runtime continuity anchor.
    AttentionCheckpoint createCheckpoint() const;
    
    // Re-evaluates focus based on a previously anchored checkpoint.
    // DOES NOT mutate state directly; preserves semantic arbitration.
    FocusDecision evaluateCheckpointRestore(const AttentionCheckpoint& checkpoint);

    // --- Semantic Continuity & Validation ---
    
    // Purges a destroyed surface from checkpoints, restore chains, modal stacks, 
    // and focus history to prevent silent continuity corruption.
    void invalidateSemanticReferences(NodeId surfaceId);

    // Defines the exact semantic fallback when OS activation fails or diverges.
    // Context is tracked in DivergenceContext to prevent infinite recursion.
    FocusDecision resolveDivergence(FocusDivergence divergence, NodeId contextSurface, DivergenceContext& ctx);

    // --- Observability (NOT Authority) ---
    
    // Called when the Win32 ActivationManager confirms a transition succeeded.
    // This updates currentFocus_ and previousFocus_ semantic truths.
    void commitRealizedActivation(NodeId realizedId, EpochId epoch);
    
    // Called when the OS disagrees with our requested semantic transition.
    void commitDivergedActivation(FocusDivergence divergence, EpochId epoch);

    // --- Querying ---
    
    bool isEligible(NodeId id) const;
    bool isSuppressedByModal(NodeId id) const;
    std::optional<FocusNode> getNode(NodeId id) const;

private:
    // Semantic State
    NodeId currentFocus_ = kInvalidNodeId;
    NodeId previousFocus_ = kInvalidNodeId;

    // Phase 5B: Per-workspace chains (keyed by WorkspaceId::value)
    std::unordered_map<uint64_t, std::vector<NodeId>> workspaceChains_;
    uint64_t activeWorkspaceKey_ = 1;  // default workspace
    std::vector<NodeId> detachedChain_;
    std::vector<ModalSuppressionPolicy> modalStack_;
    
    std::unordered_map<NodeId, FocusNode> nodes_;
    
    EpochId currentEpoch_ = 0;
    ContinuityState continuityState_ = ContinuityState::Coherent;
    
    // Empty chain sentinel for const& returns
    static const std::vector<NodeId> emptyChain_;

    // Internal Helpers
    EpochId nextEpoch();
    FocusDecision buildDecision(NodeId target, FocusInitiator initiator, bool requiresRestore = false);
    
    // Policy evaluators
    FocusDecision evaluateCycleForward();
    FocusDecision evaluateCycleBackward();
    FocusDecision evaluateActivateDetached();
    FocusDecision evaluateFocusWorkspace();
};

} // namespace morphic

#endif // MORPHIC_SRC_INTERACTION_FOCUS_GRAPH_H_
