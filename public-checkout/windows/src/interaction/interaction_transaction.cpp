#include "interaction_transaction.h"
#include "focus_graph.h"
#include "../composition/compositor.h"

namespace morphic {

InteractionTransaction::InteractionTransaction(FocusGraph& graph, Compositor& compositor)
    : graph_(graph), compositor_(compositor) {
    // Phase 5A: Capture pre-transaction state for rollback
    preTransactionCheckpoint_ = graph_.createCheckpoint();
}

InteractionTransaction::~InteractionTransaction() {
    if (!committed_ && !rolledBack_) {
        abort();
    }
}

void InteractionTransaction::stageIntent(InteractionIntent intent, FocusInitiator initiator) {
    FocusDecision decision = graph_.evaluateTransition(intent, initiator);
    decisions_.push_back(decision);
}

void InteractionTransaction::commit() {
    if (committed_ || rolledBack_) return;
    committed_ = true;

    if (!decisions_.empty()) {
        const FocusDecision& finalDecision = decisions_.back();
        compositor_.realizeFocusDecision(finalDecision);
    }
}

void InteractionTransaction::abort() {
    decisions_.clear();
}

void InteractionTransaction::rollback() {
    if (committed_ || rolledBack_) return;
    rolledBack_ = true;

    // Phase 5A: Transactional semantic reversal.
    // This directly restores pre-transaction state WITHOUT arbitration.
    // This is NOT evaluateCheckpointRestore (which is continuity reconstruction).
    switch (rollbackPolicy_) {
        case TransactionRollbackPolicy::RevertDirect:
            // Restore focus directly to where it was before this transaction
            if (preTransactionCheckpoint_.valid &&
                preTransactionCheckpoint_.semanticFocus != kInvalidNodeId) {
                FocusDecision revert;
                revert.target = preTransactionCheckpoint_.semanticFocus;
                revert.initiator = FocusInitiator::RecoveryRestore;
                revert.eligibility = FocusEligibility::Eligible;
                revert.behavior = AttentionBehavior::Interactive;
                revert.expectedActivation = ActivationResult::Succeeded;
                revert.requiresRestore = true;
                revert.requiresModalDismissal = false;
                compositor_.realizeFocusDecision(revert);
            }
            break;
        case TransactionRollbackPolicy::FallbackWorkspace: {
            FocusDecision fallback = graph_.evaluateRestoration(FocusRestorePolicy::WorkspaceDefault);
            compositor_.realizeFocusDecision(fallback);
            break;
        }
        case TransactionRollbackPolicy::AbandonSilently:
            // Do nothing — semantic graph may be inconsistent
            break;
    }

    decisions_.clear();
}

} // namespace morphic
