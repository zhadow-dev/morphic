#pragma once

#include "../core/interaction_phase.h"

#include <windows.h>
#include <array>
#include <cmath>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>

namespace morphic {

// Phase 8B.6 — Temporal Integrity Auditor.
//
// This is NOT FPS measurement.
// This is NOT convergence tracking (that's TemporalValidator).
// This is: runtime orchestration timing integrity.
//
// Tracks tick-level cadence to answer:
//   "Is the orchestration clock delivering ticks with the temporal
//    guarantees the runtime needs for stable interaction?"
//
// Threat model:
//   Correctness is intact today. But when extraction, docking, and
//   compositor coordination arrive, they multiply orchestration load.
//   If temporal integrity is ignored now, future phases become visually
//   unstable even if logically correct.
//
// GOVERNING RULE:
//   This auditor is PURELY OBSERVATIONAL.
//   It must NOT affect cadence, ordering, interaction timing, or state.
//   The moment instrumentation affects what it measures, the data lies.
//
// Every timing sample carries an InteractionPhase tag.
// Without phase attribution, averages are meaningless and spikes unactionable.
class TemporalAuditor {
public:
    // --- Severity tiers ---
    // These are NOT arbitrary. They correspond to perceptual thresholds:
    //   32ms = 2x 16ms target → noticeable stutter during drag
    //   50ms = dropped frame cluster → visible jank
    //  100ms = user-perceptible stall → feels broken
    enum class Severity {
        Normal,     // ≤ 32ms
        Warn,       // > 32ms
        Alert,      // > 50ms
        Fail        // > 100ms
    };

    static Severity classify(double tickMs) {
        if (tickMs > 100.0) return Severity::Fail;
        if (tickMs > 50.0)  return Severity::Alert;
        if (tickMs > 32.0)  return Severity::Warn;
        return Severity::Normal;
    }

    static const char* toString(Severity s) {
        switch (s) {
            case Severity::Normal: return "NORMAL";
            case Severity::Warn:   return "WARN";
            case Severity::Alert:  return "ALERT";
            case Severity::Fail:   return "FAIL";
        }
        return "UNKNOWN";
    }

    // --- Per-phase timing bucket ---
    struct PhaseBucket {
        int sampleCount = 0;
        double sumMs = 0.0;
        double peakMs = 0.0;
        int warnCount = 0;      // > 32ms
        int alertCount = 0;     // > 50ms
        int failCount = 0;      // > 100ms
        int longestWarnStreak = 0;
        int longestAlertStreak = 0;
        int longestFailStreak = 0;

        double avgMs() const {
            return sampleCount > 0 ? sumMs / sampleCount : 0.0;
        }
    };

    // --- Tick sample (phase-attributed) ---
    struct TickSample {
        double tickMs = 0.0;
        InteractionPhase phase = InteractionPhase::Idle;
        Severity severity = Severity::Normal;
        uint64_t tickNumber = 0;
    };

    // --- Aggregated report ---
    struct TemporalReport {
        uint64_t totalTicks = 0;
        double avgTickMs = 0.0;
        double peakTickMs = 0.0;
        double jitterStdDev = 0.0;

        // Global severity counts
        int warnCount = 0;
        int alertCount = 0;
        int failCount = 0;

        // Global streak tracking
        int longestWarnStreak = 0;
        int longestAlertStreak = 0;
        int longestFailStreak = 0;

        // Burst stall: ≥3 consecutive ticks >32ms
        int burstStallCount = 0;

        // Timer drift: cumulative expected vs observed
        double timerDriftMs = 0.0;

        // Per-phase breakdown
        static constexpr int kPhaseCount = 13;
        PhaseBucket phases[kPhaseCount] = {};

        const PhaseBucket& phaseStats(InteractionPhase p) const {
            return phases[static_cast<int>(p)];
        }
    };

    TemporalAuditor() {
        QueryPerformanceFrequency(&qpcFreq_);
    }

    // --- Core API: call once per tick ---
    //
    // tickMs: wall-clock duration of this tick (from QPC measurement)
    // phase: what the runtime was doing during this tick
    void recordTick(double tickMs, InteractionPhase phase) {
        totalTicks_++;
        Severity sev = classify(tickMs);

        // --- Global EMA average (alpha=0.02 for 120-sample window) ---
        if (totalTicks_ == 1) {
            emaAvg_ = tickMs;
        } else {
            emaAvg_ = emaAvg_ * (1.0 - kEmaAlpha) + tickMs * kEmaAlpha;
        }

        // --- Global peak ---
        if (tickMs > globalPeak_) {
            globalPeak_ = tickMs;
        }

        // --- Jitter tracking (Welford's online variance) ---
        double delta = tickMs - welfordMean_;
        welfordMean_ += delta / static_cast<double>(totalTicks_);
        double delta2 = tickMs - welfordMean_;
        welfordM2_ += delta * delta2;

        // --- Severity counting ---
        if (sev >= Severity::Warn) {
            warnCount_++;
            currentWarnStreak_++;
            if (currentWarnStreak_ > longestWarnStreak_) longestWarnStreak_ = currentWarnStreak_;
        } else {
            currentWarnStreak_ = 0;
        }

        if (sev >= Severity::Alert) {
            alertCount_++;
            currentAlertStreak_++;
            if (currentAlertStreak_ > longestAlertStreak_) longestAlertStreak_ = currentAlertStreak_;
        } else {
            currentAlertStreak_ = 0;
        }

        if (sev >= Severity::Fail) {
            failCount_++;
            currentFailStreak_++;
            if (currentFailStreak_ > longestFailStreak_) longestFailStreak_ = currentFailStreak_;
        } else {
            currentFailStreak_ = 0;
        }

        // --- Burst stall detection: ≥3 consecutive ticks >32ms ---
        if (sev >= Severity::Warn) {
            consecutiveLateCount_++;
            if (consecutiveLateCount_ == 3) {
                burstStallCount_++;
            } else if (consecutiveLateCount_ > 3) {
                // Already counted this burst, don't re-count
            }
        } else {
            consecutiveLateCount_ = 0;
        }

        // --- Timer drift: cumulative expected vs observed ---
        // Expected cadence: targetTickMs_ per tick
        // Actual: sum of all tick durations
        cumulativeActualMs_ += tickMs;
        double cumulativeExpectedMs = totalTicks_ * targetTickMs_;
        timerDrift_ = cumulativeActualMs_ - cumulativeExpectedMs;

        // --- Per-phase bucket ---
        int phaseIdx = static_cast<int>(phase);
        if (phaseIdx >= 0 && phaseIdx < TemporalReport::kPhaseCount) {
            auto& bucket = phaseBuckets_[phaseIdx];
            bucket.sampleCount++;
            bucket.sumMs += tickMs;
            if (tickMs > bucket.peakMs) bucket.peakMs = tickMs;

            if (sev >= Severity::Warn) {
                bucket.warnCount++;
                phaseWarnStreaks_[phaseIdx]++;
                if (phaseWarnStreaks_[phaseIdx] > bucket.longestWarnStreak)
                    bucket.longestWarnStreak = phaseWarnStreaks_[phaseIdx];
            } else {
                phaseWarnStreaks_[phaseIdx] = 0;
            }

            if (sev >= Severity::Alert) {
                bucket.alertCount++;
                phaseAlertStreaks_[phaseIdx]++;
                if (phaseAlertStreaks_[phaseIdx] > bucket.longestAlertStreak)
                    bucket.longestAlertStreak = phaseAlertStreaks_[phaseIdx];
            } else {
                phaseAlertStreaks_[phaseIdx] = 0;
            }

            if (sev >= Severity::Fail) {
                bucket.failCount++;
                phaseFailStreaks_[phaseIdx]++;
                if (phaseFailStreaks_[phaseIdx] > bucket.longestFailStreak)
                    bucket.longestFailStreak = phaseFailStreaks_[phaseIdx];
            } else {
                phaseFailStreaks_[phaseIdx] = 0;
            }
        }

        // --- Throttled diagnostic emission ---
        // At most every 60 ticks to avoid spam
        ticksSinceLastEmit_++;
        if (ticksSinceLastEmit_ >= 60 && sev >= Severity::Warn) {
            emitDiagnostic(tickMs, phase, sev);
            ticksSinceLastEmit_ = 0;
        }
    }

    // --- Generate report ---
    TemporalReport report() const {
        TemporalReport r;
        r.totalTicks = totalTicks_;
        r.avgTickMs = emaAvg_;
        r.peakTickMs = globalPeak_;
        r.jitterStdDev = (totalTicks_ > 1) ? std::sqrt(welfordM2_ / (totalTicks_ - 1)) : 0.0;
        r.warnCount = warnCount_;
        r.alertCount = alertCount_;
        r.failCount = failCount_;
        r.longestWarnStreak = longestWarnStreak_;
        r.longestAlertStreak = longestAlertStreak_;
        r.longestFailStreak = longestFailStreak_;
        r.burstStallCount = burstStallCount_;
        r.timerDriftMs = timerDrift_;
        for (int i = 0; i < TemporalReport::kPhaseCount; i++) {
            r.phases[i] = phaseBuckets_[i];
        }
        return r;
    }

    // --- Serialize report to JSON file ---
    bool writeReport(const std::string& path) const {
        std::ofstream out(path);
        if (!out.is_open()) return false;

        auto r = report();
        out << "{\n";
        out << "  \"total_ticks\": " << r.totalTicks << ",\n";
        out << "  \"avg_tick_ms\": " << r.avgTickMs << ",\n";
        out << "  \"peak_tick_ms\": " << r.peakTickMs << ",\n";
        out << "  \"jitter_stddev_ms\": " << r.jitterStdDev << ",\n";
        out << "  \"warn_count\": " << r.warnCount << ",\n";
        out << "  \"alert_count\": " << r.alertCount << ",\n";
        out << "  \"fail_count\": " << r.failCount << ",\n";
        out << "  \"longest_warn_streak\": " << r.longestWarnStreak << ",\n";
        out << "  \"longest_alert_streak\": " << r.longestAlertStreak << ",\n";
        out << "  \"longest_fail_streak\": " << r.longestFailStreak << ",\n";
        out << "  \"burst_stall_count\": " << r.burstStallCount << ",\n";
        out << "  \"timer_drift_ms\": " << r.timerDriftMs << ",\n";
        out << "  \"phases\": {\n";

        const char* phaseNames[] = {
            "idle", "drag", "resize", "grouped_drag", "capture_loss",
            "destroy", "reconcile", "projection", "audit", "replay",
            "stress", "activation", "governance"
        };

        bool first = true;
        for (int i = 0; i < TemporalReport::kPhaseCount; i++) {
            const auto& b = r.phases[i];
            if (b.sampleCount == 0) continue;
            if (!first) out << ",\n";
            first = false;
            out << "    \"" << phaseNames[i] << "\": {"
                << " \"samples\": " << b.sampleCount
                << ", \"avg_ms\": " << b.avgMs()
                << ", \"peak_ms\": " << b.peakMs
                << ", \"warn\": " << b.warnCount
                << ", \"alert\": " << b.alertCount
                << ", \"fail\": " << b.failCount
                << ", \"longest_warn_streak\": " << b.longestWarnStreak
                << ", \"longest_alert_streak\": " << b.longestAlertStreak
                << ", \"longest_fail_streak\": " << b.longestFailStreak
                << " }";
        }
        out << "\n  }\n";
        out << "}\n";
        return true;
    }

    void setTargetTickMs(double ms) { targetTickMs_ = ms; }

    void reset() {
        totalTicks_ = 0;
        emaAvg_ = 0.0;
        globalPeak_ = 0.0;
        welfordMean_ = 0.0;
        welfordM2_ = 0.0;
        warnCount_ = 0;
        alertCount_ = 0;
        failCount_ = 0;
        currentWarnStreak_ = 0;
        currentAlertStreak_ = 0;
        currentFailStreak_ = 0;
        longestWarnStreak_ = 0;
        longestAlertStreak_ = 0;
        longestFailStreak_ = 0;
        consecutiveLateCount_ = 0;
        burstStallCount_ = 0;
        cumulativeActualMs_ = 0.0;
        timerDrift_ = 0.0;
        ticksSinceLastEmit_ = 0;
        for (auto& b : phaseBuckets_) b = {};
        for (auto& s : phaseWarnStreaks_) s = 0;
        for (auto& s : phaseAlertStreaks_) s = 0;
        for (auto& s : phaseFailStreaks_) s = 0;
    }

private:
    void emitDiagnostic(double tickMs, InteractionPhase phase, Severity sev) {
        const char* sevStr = (sev == Severity::Fail) ? "TEMPORAL FAIL" :
                             (sev == Severity::Alert) ? "TEMPORAL ALERT" :
                             "TEMPORAL WARN";
        char buf[256];
        snprintf(buf, sizeof(buf),
            "[%s] tick=%.1fms phase=%s avg=%.1fms peak=%.1fms drift=%.1fms burst_stalls=%d\n",
            sevStr, tickMs, morphic::toString(phase),
            emaAvg_, globalPeak_, timerDrift_, burstStallCount_);
        OutputDebugStringA(buf);
    }

    static constexpr double kEmaAlpha = 1.0 / 60.0;  // ~60-sample EMA window

    LARGE_INTEGER qpcFreq_{};

    uint64_t totalTicks_ = 0;
    double emaAvg_ = 0.0;
    double globalPeak_ = 0.0;
    double targetTickMs_ = 16.67;  // 60fps default

    // Welford's online algorithm for variance
    double welfordMean_ = 0.0;
    double welfordM2_ = 0.0;

    // Severity counts
    int warnCount_ = 0;
    int alertCount_ = 0;
    int failCount_ = 0;

    // Streak tracking
    int currentWarnStreak_ = 0;
    int currentAlertStreak_ = 0;
    int currentFailStreak_ = 0;
    int longestWarnStreak_ = 0;
    int longestAlertStreak_ = 0;
    int longestFailStreak_ = 0;

    // Burst stall: ≥3 consecutive >32ms
    int consecutiveLateCount_ = 0;
    int burstStallCount_ = 0;

    // Timer drift
    double cumulativeActualMs_ = 0.0;
    double timerDrift_ = 0.0;

    // Throttled emission
    int ticksSinceLastEmit_ = 0;

    // Per-phase buckets
    PhaseBucket phaseBuckets_[TemporalReport::kPhaseCount] = {};
    int phaseWarnStreaks_[TemporalReport::kPhaseCount] = {};
    int phaseAlertStreaks_[TemporalReport::kPhaseCount] = {};
    int phaseFailStreaks_[TemporalReport::kPhaseCount] = {};
};

}  // namespace morphic
