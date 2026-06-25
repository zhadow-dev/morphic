#include "focus_graph.h"
#include <algorithm>
#include <windows.h> // Only for OutputDebugString, NO HWND MUTATIONS ALLOWED

namespace morphic {

// Phase 5B: Static empty chain sentinel
const std::vector<NodeId> FocusGraph::emptyChain_;

FocusGraph::FocusGraph() {}
FocusGraph::~FocusGraph() {}

void FocusGraph::registerNode(const FocusNode& node, uint64_t workspaceKey) {
    nodes_[node.id] = node;

    if (node.domain == FocusDomain::Workspace) {
        uint64_t key = (workspaceKey != 0) ? workspaceKey : activeWorkspaceKey_;
        workspaceChains_[key].push_back(node.id);
    } else if (node.domain == FocusDomain::Detached) {
        detachedChain_.push_back(node.id);
    }
}

void FocusGraph::unregisterNode(NodeId id) {
    nodes_.erase(id);
    invalidateSemanticReferences(id);
}

void FocusGraph::updateEligibility(NodeId id, FocusEligibility eligibility) {
    auto it = nodes_.find(id);
    if (it != nodes_.end()) {
        it->second.currentEligibility = eligibility;
    }
}

// Phase 5B: Multi-workspace support
void FocusGraph::setActiveWorkspace(uint64_t workspaceKey) {
    activeWorkspaceKey_ = workspaceKey;
}

const std::vector<NodeId>& FocusGraph::workspaceChain(uint64_t workspaceKey) const {
    auto it = workspaceChains_.find(workspaceKey);
    if (it != workspaceChains_.end()) {
        return it->second;
    }
    return emptyChain_;
}

FocusDecision FocusGraph::evaluateTransition(InteractionIntent intent, FocusInitiator initiator) {
    // 1. Check if an active modal blocks general interaction transitions
    if (!modalStack_.empty() && intent != InteractionIntent::ExitModal) {
        const auto& policy = modalStack_.back();
        if (policy.blocksWorkspace || policy.blocksDetached) {
            OutputDebugStringA("FocusGraph: Transition blocked by modal capture.\n");
            return buildDecision(policy.modalNodeId, FocusInitiator::ModalCapture);
        }
    }

    switch (intent) {
        case InteractionIntent::CycleForward:
            return evaluateCycleForward();
        case InteractionIntent::CycleBackward:
            return evaluateCycleBackward();
        case InteractionIntent::ActivateDetached:
            return evaluateActivateDetached();
        case InteractionIntent::FocusWorkspace:
            return evaluateFocusWorkspace();
        case InteractionIntent::DismissOverlay:
        case InteractionIntent::ExitModal:
            return buildDecision(previousFocus_ != kInvalidNodeId ? previousFocus_ : kInvalidNodeId, initiator, true);
        case InteractionIntent::ToggleOverview:
            return buildDecision(currentFocus_, initiator);
    }

    return buildDecision(currentFocus_, initiator);
}

FocusDecision FocusGraph::evaluateRestoration(FocusRestorePolicy policy) {
    NodeId target = kInvalidNodeId;
    const auto& activeChain = workspaceChain(activeWorkspaceKey_);

    switch (policy) {
        case FocusRestorePolicy::PreviousSemanticFocus:
            if (previousFocus_ != kInvalidNodeId && isEligible(previousFocus_)) {
                target = previousFocus_;
            } else {
                target = activeChain.empty() ? kInvalidNodeId : activeChain.front();
            }
            break;
        case FocusRestorePolicy::PreviousVisibleFocus:
            target = previousFocus_;
            break;
        case FocusRestorePolicy::WorkspaceDefault:
            target = activeChain.empty() ? kInvalidNodeId : activeChain.front();
            break;
        case FocusRestorePolicy::DetachedPriority:
            target = detachedChain_.empty() ? kInvalidNodeId : detachedChain_.back();
            if (target == kInvalidNodeId) {
                target = activeChain.empty() ? kInvalidNodeId : activeChain.front();
            }
            break;
        case FocusRestorePolicy::None:
            target = kInvalidNodeId;
            break;
    }

    if (target != kInvalidNodeId && !isEligible(target)) {
        if (!activeChain.empty()) {
            target = activeChain.front();
        } else {
            target = kInvalidNodeId;
        }
    }

    return buildDecision(target, FocusInitiator::RecoveryRestore, true);
}

void FocusGraph::pushModalSuppression(const ModalSuppressionPolicy& policy) {
    modalStack_.push_back(policy);
}

FocusDecision FocusGraph::popModalSuppression(NodeId modalNodeId) {
    modalStack_.erase(std::remove_if(modalStack_.begin(), modalStack_.end(),
        [modalNodeId](const ModalSuppressionPolicy& p) { return p.modalNodeId == modalNodeId; }), modalStack_.end());
    
    return evaluateRestoration(FocusRestorePolicy::PreviousSemanticFocus);
}

AttentionCheckpoint FocusGraph::createCheckpoint() const {
    AttentionCheckpoint cp;
    cp.semanticFocus = currentFocus_;
    cp.epoch = currentEpoch_;
    
    if (auto it = nodes_.find(currentFocus_); it != nodes_.end()) {
        cp.domain = it->second.domain;
        cp.restorePolicy = it->second.restorePolicy;
    } else {
        cp.domain = FocusDomain::Workspace;
        cp.restorePolicy = FocusRestorePolicy::WorkspaceDefault;
    }
    cp.valid = true;
    return cp;
}

FocusDecision FocusGraph::evaluateCheckpointRestore(const AttentionCheckpoint& checkpoint) {
    if (!checkpoint.valid) {
        return evaluateRestoration(FocusRestorePolicy::WorkspaceDefault);
    }
    
    if (checkpoint.semanticFocus != kInvalidNodeId && isEligible(checkpoint.semanticFocus)) {
        return buildDecision(checkpoint.semanticFocus, FocusInitiator::RecoveryRestore, true);
    }
    return evaluateRestoration(checkpoint.restorePolicy);
}

void FocusGraph::invalidateSemanticReferences(NodeId surfaceId) {
    // Phase 5A: Fracture per-node continuity before purging
    auto nodeIt = nodes_.find(surfaceId);
    if (nodeIt != nodes_.end()) {
        nodeIt->second.continuity.state = ContinuityState::Fractured;
    }

    // Phase 5B: Purge from ALL workspace chains
    for (auto& [key, chain] : workspaceChains_) {
        chain.erase(std::remove(chain.begin(), chain.end(), surfaceId), chain.end());
    }
    detachedChain_.erase(std::remove(detachedChain_.begin(), detachedChain_.end(), surfaceId), detachedChain_.end());
    
    // Purge from modals
    modalStack_.erase(std::remove_if(modalStack_.begin(), modalStack_.end(),
        [surfaceId](const ModalSuppressionPolicy& p) { return p.modalNodeId == surfaceId; }), modalStack_.end());
        
    // Invalidate live anchors
    if (currentFocus_ == surfaceId) {
        currentFocus_ = kInvalidNodeId;
    }
    if (previousFocus_ == surfaceId) {
        previousFocus_ = kInvalidNodeId;
    }
}

FocusDecision FocusGraph::resolveDivergence(FocusDivergence divergence, NodeId contextSurface, DivergenceContext& ctx) {
    if (ctx.retryCount > 3) {
        ctx.previousResolution = DivergenceResolution::Ignore;
        continuityState_ = ContinuityState::Diverged;
        OutputDebugStringA("FocusGraph: Hard divergence. Resolution aborted to prevent infinite recursion.\n");
        return buildDecision(kInvalidNodeId, FocusInitiator::SystemActivation);
    }
    
    ctx.retryCount++;
    ctx.originatingEpoch = currentEpoch_;
    
    if (divergence == FocusDivergence::OSDeniedActivation) {
        OutputDebugStringA("FocusGraph: OSDeniedActivation -> FallbackRestore.\n");
        ctx.previousResolution = DivergenceResolution::FallbackRestore;
        updateEligibility(contextSurface, FocusEligibility::DetachedInactive);
        return evaluateRestoration(FocusRestorePolicy::WorkspaceDefault);
    }
    
    ctx.previousResolution = DivergenceResolution::Ignore;
    return buildDecision(kInvalidNodeId, FocusInitiator::SystemActivation);
}

void FocusGraph::commitRealizedActivation(NodeId realizedId, EpochId epoch) {
    if (epoch >= currentEpoch_) {
        if (currentFocus_ != realizedId) {
            previousFocus_ = currentFocus_;
            currentFocus_ = realizedId;
        }
    }
}

void FocusGraph::commitDivergedActivation(FocusDivergence divergence, EpochId epoch) {
    if (divergence == FocusDivergence::OSDeniedActivation) {
        OutputDebugStringA("FocusGraph: Win32 denied activation. Semantic state is now out of sync.\n");
    } else if (divergence == FocusDivergence::ExternalFocusSteal) {
        OutputDebugStringA("FocusGraph: External app stole focus.\n");
    }
}

bool FocusGraph::isEligible(NodeId id) const {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    
    if (it->second.currentEligibility != FocusEligibility::Eligible) {
        return false;
    }

    if (isSuppressedByModal(id)) {
        return false;
    }

    return true;
}

bool FocusGraph::isSuppressedByModal(NodeId id) const {
    if (modalStack_.empty()) return false;
    
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;

    if (modalStack_.back().modalNodeId == id) return false;

    if (it->second.domain == FocusDomain::Workspace && modalStack_.back().blocksWorkspace) return true;
    if (it->second.domain == FocusDomain::Detached && modalStack_.back().blocksDetached) return true;

    return false;
}

std::optional<FocusNode> FocusGraph::getNode(NodeId id) const {
    auto it = nodes_.find(id);
    if (it != nodes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

EpochId FocusGraph::nextEpoch() {
    return ++currentEpoch_;
}

FocusDecision FocusGraph::buildDecision(NodeId target, FocusInitiator initiator, bool requiresRestore) {
    FocusDecision decision;
    decision.target = target;
    decision.initiator = initiator;
    decision.requiresRestore = requiresRestore;
    decision.requiresModalDismissal = false;

    if (auto it = nodes_.find(target); it != nodes_.end()) {
        decision.eligibility = it->second.currentEligibility;
        decision.behavior = it->second.behavior;
        decision.expectedActivation = ActivationResult::Succeeded;
    } else {
        decision.eligibility = FocusEligibility::Hidden;
        decision.behavior = AttentionBehavior::NonFocusable;
        decision.expectedActivation = ActivationResult::DeniedByOS;
    }

    return decision;
}

// Phase 5B: Cycle operates on the ACTIVE workspace chain only
FocusDecision FocusGraph::evaluateCycleForward() {
    const auto& chain = workspaceChain(activeWorkspaceKey_);
    if (chain.empty()) return buildDecision(kInvalidNodeId, FocusInitiator::UserInput);
    
    auto it = std::find(chain.begin(), chain.end(), currentFocus_);
    if (it == chain.end() || std::next(it) == chain.end()) {
        return buildDecision(chain.front(), FocusInitiator::UserInput);
    }
    return buildDecision(*std::next(it), FocusInitiator::UserInput);
}

FocusDecision FocusGraph::evaluateCycleBackward() {
    const auto& chain = workspaceChain(activeWorkspaceKey_);
    if (chain.empty()) return buildDecision(kInvalidNodeId, FocusInitiator::UserInput);
    
    auto it = std::find(chain.begin(), chain.end(), currentFocus_);
    if (it == chain.end() || it == chain.begin()) {
        return buildDecision(chain.back(), FocusInitiator::UserInput);
    }
    return buildDecision(*std::prev(it), FocusInitiator::UserInput);
}

FocusDecision FocusGraph::evaluateActivateDetached() {
    if (detachedChain_.empty()) return buildDecision(currentFocus_, FocusInitiator::UserInput);
    return buildDecision(detachedChain_.back(), FocusInitiator::UserInput);
}

FocusDecision FocusGraph::evaluateFocusWorkspace() {
    const auto& chain = workspaceChain(activeWorkspaceKey_);
    if (chain.empty()) return buildDecision(currentFocus_, FocusInitiator::UserInput);
    return buildDecision(chain.front(), FocusInitiator::UserInput);
}

} // namespace morphic
