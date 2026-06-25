#ifndef MORPHIC_SRC_INTERACTION_INTERACTION_TRANSACTION_H_
#define MORPHIC_SRC_INTERACTION_INTERACTION_TRANSACTION_H_

#include "focus_types.h"
#include <vector>

namespace morphic {

class FocusGraph;
class InputRouter;
class Compositor;

// Phase 5A: Transaction Rollback Policy.
// Rollback is NOT restoration. Rollback reverses mutations that never became
// authoritative. Restoration reconstructs desired continuity.
enum class TransactionRollbackPolicy {
    RevertDirect,       // Undo staged mutations by restoring pre-transaction state
    FallbackWorkspace,  // Revert to workspace default focus
    AbandonSilently     // Discard without any state correction
};

// Phase 4 Step 4A / Phase 5A: Interaction Transaction
//
// A semantic mutation sandbox. Groups a sequence of intents/transitions,
// isolates graph mutations, defers activation realization, stages topology changes,
// and buffers checkpoints until an atomic commit.
//
// Phase 5A correction: captures a pre-transaction checkpoint at construction
// and supports explicit rollback (transactional reversal) separate from
// FocusGraph::evaluateCheckpointRestore (continuity reconstruction).
class InteractionTransaction {
public:
    InteractionTransaction(FocusGraph& graph, Compositor& compositor);
    ~InteractionTransaction();

    // Stage an intent to be evaluated.
    void stageIntent(InteractionIntent intent, FocusInitiator initiator);

    // Commit the staged mutations to semantic truth, then realize via Compositor.
    void commit();

    // Abort the transaction, leaving semantic truth unchanged.
    void abort();

    // Phase 5A: Transactional semantic reversal.
    // Reverses staged mutations by restoring pre-transaction graph state DIRECTLY.
    // This is NOT evaluateCheckpointRestore — it does not go through arbitration.
    // Use this when a transaction fails mid-flight and the graph must be unwound.
    void rollback();

    void setRollbackPolicy(TransactionRollbackPolicy policy) { rollbackPolicy_ = policy; }

private:
    FocusGraph& graph_;
    Compositor& compositor_;
    bool committed_ = false;
    bool rolledBack_ = false;

    // Phase 5A: Pre-transaction anchor for rollback
    AttentionCheckpoint preTransactionCheckpoint_;
    TransactionRollbackPolicy rollbackPolicy_ = TransactionRollbackPolicy::RevertDirect;

    // We buffer decisions and intents.
    std::vector<FocusDecision> decisions_;
};

} // namespace morphic

#endif // MORPHIC_SRC_INTERACTION_INTERACTION_TRANSACTION_H_
