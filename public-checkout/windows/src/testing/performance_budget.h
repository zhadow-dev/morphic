#pragma once

#include "../core/interaction_phase.h"
#include "../core/runtime_commit_scheduler.h"
#include "../core/runtime_events.h"

#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>

namespace morphic {

// Phase 8B.6 — Performance Budget Monitor.
//
// Formalized orchestration budget tracking.
// This is NOT optimization. This is early-warning instrumentation.
//
// When a budget is exceeded, the monitor emits a warning.
// It does NOT throttle, drop, or modify behavior.
// It exists so future phases can detect regressions before
// they manifest as visual instability.
//
// Warnings are throttled (every 120 ticks) to avoid spam.
//
// OBSERVATIONAL ONLY. No mutations, no authority, no timing effects.
class PerformanceBudgetMonitor {
public:
    struct Budgets {
        size_t maxProjectionsPerTick = 15;
        size_t maxDroppedProjections = 0;
        double maxCoalesceRatio = 0.50;           // 50% coalesced = too much churn
        size_t maxEventFanoutListeners = 20;
        size_t maxSubscribersPerSession = 30;
        double maxTransactionDurationMs = 8.0;
        double maxAuditCostMs = 2.0;
    };

    struct BudgetViolation {
        std::string metric;
        std::string details;
        InteractionPhase phase = InteractionPhase::Idle;
        uint64_t tickNumber = 0;
    };

    struct BudgetReport {
        uint64_t totalTicks = 0;

        // Violation counts per metric
        int projectionFanoutViolations = 0;
        int droppedProjectionViolations = 0;
        int coalesceRatioViolations = 0;
        int eventFanoutViolations = 0;
        int subscriberViolations = 0;
        int transactionDurationViolations = 0;
        int auditCostViolations = 0;

        int totalViolations() const {
            return projectionFanoutViolations +
                   droppedProjectionViolations +
                   coalesceRatioViolations +
                   eventFanoutViolations +
                   subscriberViolations +
                   transactionDurationViolations +
                   auditCostViolations;
        }
    };

    PerformanceBudgetMonitor() = default;

    // --- Core API: call once per tick with current metrics ---
    void evaluateTick(
        uint64_t tickNumber,
        InteractionPhase phase,
        size_t projectionsThisTick,
        size_t droppedProjections,
        size_t totalEnqueued,
        size_t coalescedCount,
        size_t maxEventListeners,
        size_t totalSubscribers,
        double transactionDurationMs,
        double auditCostMs)
    {
        totalTicks_++;
        ticksSinceLastEmit_++;

        bool anyViolation = false;

        // Projection fanout
        if (projectionsThisTick > budgets_.maxProjectionsPerTick) {
            report_.projectionFanoutViolations++;
            anyViolation = true;
            if (shouldEmit()) {
                emitWarn("projection_fanout",
                    std::to_string(projectionsThisTick) + " > " +
                    std::to_string(budgets_.maxProjectionsPerTick), phase);
            }
        }

        // Dropped projections
        if (droppedProjections > budgets_.maxDroppedProjections) {
            report_.droppedProjectionViolations++;
            anyViolation = true;
            if (shouldEmit()) {
                emitWarn("dropped_projections",
                    std::to_string(droppedProjections) + " dropped", phase);
            }
        }

        // Coalesce ratio
        if (totalEnqueued > 0) {
            double ratio = static_cast<double>(coalescedCount) / static_cast<double>(totalEnqueued);
            if (ratio > budgets_.maxCoalesceRatio) {
                report_.coalesceRatioViolations++;
                anyViolation = true;
                if (shouldEmit()) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.1f%% > %.1f%%",
                        ratio * 100.0, budgets_.maxCoalesceRatio * 100.0);
                    emitWarn("coalesce_ratio", buf, phase);
                }
            }
        }

        // Event fanout
        if (maxEventListeners > budgets_.maxEventFanoutListeners) {
            report_.eventFanoutViolations++;
            anyViolation = true;
            if (shouldEmit()) {
                emitWarn("event_fanout",
                    std::to_string(maxEventListeners) + " listeners > " +
                    std::to_string(budgets_.maxEventFanoutListeners), phase);
            }
        }

        // Subscriber count
        if (totalSubscribers > budgets_.maxSubscribersPerSession) {
            report_.subscriberViolations++;
            anyViolation = true;
            if (shouldEmit()) {
                emitWarn("subscriber_count",
                    std::to_string(totalSubscribers) + " > " +
                    std::to_string(budgets_.maxSubscribersPerSession), phase);
            }
        }

        // Transaction duration
        if (transactionDurationMs > budgets_.maxTransactionDurationMs) {
            report_.transactionDurationViolations++;
            anyViolation = true;
            if (shouldEmit()) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.2fms > %.2fms",
                    transactionDurationMs, budgets_.maxTransactionDurationMs);
                emitWarn("transaction_duration", buf, phase);
            }
        }

        // Audit cost
        if (auditCostMs > budgets_.maxAuditCostMs) {
            report_.auditCostViolations++;
            anyViolation = true;
            if (shouldEmit()) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.2fms > %.2fms",
                    auditCostMs, budgets_.maxAuditCostMs);
                emitWarn("audit_cost", buf, phase);
            }
        }

        if (anyViolation && shouldEmit()) {
            ticksSinceLastEmit_ = 0;
        }

        report_.totalTicks = totalTicks_;
    }

    const BudgetReport& currentReport() const { return report_; }
    Budgets& mutableBudgets() { return budgets_; }
    const Budgets& budgets() const { return budgets_; }

    bool writeReport(const std::string& path) const {
        std::ofstream out(path);
        if (!out.is_open()) return false;

        out << "{\n";
        out << "  \"total_ticks\": " << report_.totalTicks << ",\n";
        out << "  \"violations\": {\n";
        out << "    \"projection_fanout\": " << report_.projectionFanoutViolations << ",\n";
        out << "    \"dropped_projections\": " << report_.droppedProjectionViolations << ",\n";
        out << "    \"coalesce_ratio\": " << report_.coalesceRatioViolations << ",\n";
        out << "    \"event_fanout\": " << report_.eventFanoutViolations << ",\n";
        out << "    \"subscriber_count\": " << report_.subscriberViolations << ",\n";
        out << "    \"transaction_duration\": " << report_.transactionDurationViolations << ",\n";
        out << "    \"audit_cost\": " << report_.auditCostViolations << ",\n";
        out << "    \"total\": " << report_.totalViolations() << "\n";
        out << "  },\n";
        out << "  \"budgets\": {\n";
        out << "    \"max_projections_per_tick\": " << budgets_.maxProjectionsPerTick << ",\n";
        out << "    \"max_dropped_projections\": " << budgets_.maxDroppedProjections << ",\n";
        out << "    \"max_coalesce_ratio\": " << budgets_.maxCoalesceRatio << ",\n";
        out << "    \"max_event_fanout_listeners\": " << budgets_.maxEventFanoutListeners << ",\n";
        out << "    \"max_subscribers_per_session\": " << budgets_.maxSubscribersPerSession << ",\n";
        out << "    \"max_transaction_duration_ms\": " << budgets_.maxTransactionDurationMs << ",\n";
        out << "    \"max_audit_cost_ms\": " << budgets_.maxAuditCostMs << "\n";
        out << "  }\n";
        out << "}\n";
        return true;
    }

    void reset() {
        report_ = {};
        totalTicks_ = 0;
        ticksSinceLastEmit_ = 0;
    }

private:
    bool shouldEmit() const {
        return ticksSinceLastEmit_ >= 120;
    }

    void emitWarn(const char* metric, const std::string& details, InteractionPhase phase) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "[PERF WARN] %s exceeded budget: %s (phase=%s)\n",
            metric, details.c_str(), morphic::toString(phase));
        OutputDebugStringA(buf);
    }

    Budgets budgets_;
    BudgetReport report_;
    uint64_t totalTicks_ = 0;
    int ticksSinceLastEmit_ = 0;
};

}  // namespace morphic
