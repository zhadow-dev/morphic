#pragma once

#include "thread_affinity.h"
#include <windows.h>
#include <string>

namespace morphic {

// Phase 4 Step 1 — Runtime mutation authority enforcement.
//
// Every mutation to runtime state MUST pass through its designated authority.
// This is NOT documentation. This is RUNTIME ENFORCEMENT.
//
// If a subsystem mutates state it does not own, the assertion fires.
// This prevents fake architecture where rules exist but aren't enforced.
//
// MUTATION TYPES:
//   Topology      — SetParent, SetWindowLongPtr, style changes, owner changes
//   Activation    — SetWindowPos (z-order), DeferWindowPos, SetForegroundWindow
//   Visibility    — ShowWindow, hiding/showing surfaces
//   Focus         — semantic focus decisions (who SHOULD be focused)
//   RendererBinding — binding/unbinding renderers to surfaces
//   WorkspaceMembership — adding/removing surfaces from workspaces

enum class RuntimeMutationType {
    Topology,
    Activation,
    Visibility,
    Focus,
    RendererBinding,
    WorkspaceMembership,
};

inline const char* toString(RuntimeMutationType t) {
    switch (t) {
        case RuntimeMutationType::Topology:            return "topology";
        case RuntimeMutationType::Activation:          return "activation";
        case RuntimeMutationType::Visibility:          return "visibility";
        case RuntimeMutationType::Focus:               return "focus";
        case RuntimeMutationType::RendererBinding:     return "rendererBinding";
        case RuntimeMutationType::WorkspaceMembership: return "workspaceMembership";
    }
    return "unknown";
}

// --- Mutation Authority Tracking ---
//
// Subsystems MUST acquire authority before performing mutations.
// This uses a thread-local stack so nested authority is detectable.
//
// Pattern:
//   MutationAuthorityGuard guard(RuntimeMutationType::Topology);
//   // now SetParent, SetWindowLongPtr etc. are legal
//   // guard destructor releases authority
//
// Assertion macros check that the correct authority is held
// before performing a mutation.

class MutationAuthorityTracker {
public:
    static void acquire(RuntimeMutationType type, const char* subsystem) {
        if (activeCount_ < kMaxDepth) {
            stack_[activeCount_] = { type, subsystem };
        }
        activeCount_++;

        OutputDebugStringA(("MUTATION_AUTH: acquired " +
            std::string(toString(type)) + " by " + subsystem + "\n").c_str());
    }

    static void release(RuntimeMutationType type) {
        if (activeCount_ > 0) {
            activeCount_--;
        }
    }

    static bool isHeld(RuntimeMutationType type) {
        for (int i = 0; i < activeCount_ && i < kMaxDepth; i++) {
            if (stack_[i].type == type) return true;
        }
        return false;
    }

    static void assertAuthority(RuntimeMutationType type, const char* context) {
#ifndef NDEBUG
        if (!isHeld(type)) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                "MUTATION AUTHORITY VIOLATION: %s attempted %s mutation "
                "without acquiring authority. Current authorities: ",
                context, toString(type));
            std::string msg(buf);
            if (activeCount_ == 0) {
                msg += "NONE";
            } else {
                for (int i = 0; i < activeCount_ && i < kMaxDepth; i++) {
                    if (i > 0) msg += ", ";
                    msg += toString(stack_[i].type);
                    msg += "(";
                    msg += stack_[i].subsystem;
                    msg += ")";
                }
            }
            msg += "\n";
            OutputDebugStringA(msg.c_str());
            assert(false && "Mutation authority violation — see OutputDebugString");
        }
#endif
    }

private:
    struct AuthEntry {
        RuntimeMutationType type;
        const char* subsystem;
    };

    static constexpr int kMaxDepth = 8;
    static inline thread_local AuthEntry stack_[kMaxDepth] = {};
    static inline thread_local int activeCount_ = 0;
};

// RAII guard for mutation authority.
class MutationAuthorityGuard {
public:
    MutationAuthorityGuard(RuntimeMutationType type, const char* subsystem)
        : type_(type) {
        MutationAuthorityTracker::acquire(type, subsystem);
    }

    ~MutationAuthorityGuard() {
        MutationAuthorityTracker::release(type_);
    }

    // Non-copyable
    MutationAuthorityGuard(const MutationAuthorityGuard&) = delete;
    MutationAuthorityGuard& operator=(const MutationAuthorityGuard&) = delete;

private:
    RuntimeMutationType type_;
};

// --- Assertion Macros ---
// Use these at the START of any function that performs a mutation.
// Zero cost in release builds.

#define MORPHIC_ASSERT_TOPOLOGY_AUTHORITY() \
    ::morphic::MutationAuthorityTracker::assertAuthority( \
        ::morphic::RuntimeMutationType::Topology, __FUNCTION__)

#define MORPHIC_ASSERT_ACTIVATION_AUTHORITY() \
    ::morphic::MutationAuthorityTracker::assertAuthority( \
        ::morphic::RuntimeMutationType::Activation, __FUNCTION__)

#define MORPHIC_ASSERT_VISIBILITY_AUTHORITY() \
    ::morphic::MutationAuthorityTracker::assertAuthority( \
        ::morphic::RuntimeMutationType::Visibility, __FUNCTION__)

#define MORPHIC_ASSERT_FOCUS_AUTHORITY() \
    ::morphic::MutationAuthorityTracker::assertAuthority( \
        ::morphic::RuntimeMutationType::Focus, __FUNCTION__)

#define MORPHIC_ASSERT_RENDERER_BINDING_AUTHORITY() \
    ::morphic::MutationAuthorityTracker::assertAuthority( \
        ::morphic::RuntimeMutationType::RendererBinding, __FUNCTION__)

// Convenience: acquire + auto-release via RAII
#define MORPHIC_ACQUIRE_TOPOLOGY_AUTHORITY() \
    ::morphic::MutationAuthorityGuard _topoGuard_( \
        ::morphic::RuntimeMutationType::Topology, __FUNCTION__)

#define MORPHIC_ACQUIRE_ACTIVATION_AUTHORITY() \
    ::morphic::MutationAuthorityGuard _actGuard_( \
        ::morphic::RuntimeMutationType::Activation, __FUNCTION__)

#define MORPHIC_ACQUIRE_VISIBILITY_AUTHORITY() \
    ::morphic::MutationAuthorityGuard _visGuard_( \
        ::morphic::RuntimeMutationType::Visibility, __FUNCTION__)

}  // namespace morphic
