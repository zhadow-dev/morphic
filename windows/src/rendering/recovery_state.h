#pragma once

#include "../core/types.h"
#include <chrono>
#include <string>
#include <algorithm>
#include <cmath>
#include <vector>
#include <numeric>

namespace morphic {

// Phase 3C — Recovery & Continuity Telemetry
//
// NOT frame-time measurement.
// This is EXPERIENTIAL CONTINUITY INSTRUMENTATION.
//
// Measures:
//   - perceived wake smoothness
//   - stale-frame persistence
//   - animation continuity gaps
//   - visual discontinuity
//   - recovery cadence stabilization
//   - post-resume pacing variance
//   - visual silence (renderer alive but no meaningful update)
//
// Architecture:
//   RecoveryPhase    → lifecycle phases of a recovery event
//   RecoveryClass    → perceptual classification of wake quality
//   RecoveryTracker  → per-renderer state machine for recovery tracking
//   RecoveryReport   → aggregate experiential summary

// ============================================================
//  Recovery Lifecycle Phases
// ============================================================

// Each renderer goes through these phases during wake.
// Phase transitions are driven by frame production evidence,
// NOT by timer expiration.
enum class RecoveryPhase {
    Stable,           // Normal operation — producing frames at cadence
    RecoveringCold,   // Renderer waking from Parked/Dormant — no frames yet
    RecoveringWarm,   // First frame received, cadence not yet stabilized
    VisuallyStable,   // Cadence normalized — user perceives smooth output
    StaleVisible,     // Old frame still displayed — renderer not producing
    ContinuityBroken, // User-visible discontinuity detected (jump/hitch)
};

inline const char* toString(RecoveryPhase p) {
    switch (p) {
        case RecoveryPhase::Stable:           return "Stable";
        case RecoveryPhase::RecoveringCold:   return "RecoveringCold";
        case RecoveryPhase::RecoveringWarm:   return "RecoveringWarm";
        case RecoveryPhase::VisuallyStable:   return "VisuallyStable";
        case RecoveryPhase::StaleVisible:     return "StaleVisible";
        case RecoveryPhase::ContinuityBroken: return "ContinuityBroken";
    }
    return "?";
}

// ============================================================
//  Experiential Quality Classification
// ============================================================

// Perceptual wake classification.
// Humans perceive continuity NONLINEARLY:
//   <16ms  → imperceptible (sub-frame)
//   <50ms  → smooth (within single vsync)
//   <150ms → hitchy (visible jolt but recoverable)
//   >150ms → disruptive (obvious interruption)
enum class RecoveryClass {
    Instant,     // <16ms — imperceptible
    Smooth,      // 16-50ms — noticeable but acceptable
    Hitchy,      // 50-150ms — visible discontinuity
    Disruptive,  // >150ms — obvious interruption
    Unknown,     // Not yet classified
};

inline const char* toString(RecoveryClass c) {
    switch (c) {
        case RecoveryClass::Instant:    return "Instant";
        case RecoveryClass::Smooth:     return "Smooth";
        case RecoveryClass::Hitchy:     return "Hitchy";
        case RecoveryClass::Disruptive: return "Disruptive";
        case RecoveryClass::Unknown:    return "Unknown";
    }
    return "?";
}

// Perceptual thresholds (milliseconds).
// These are NOT frame-time budgets.
// These are HUMAN PERCEPTION thresholds.
struct PerceptualThresholds {
    static constexpr double kInstantMs     = 16.0;   // Sub-frame — imperceptible
    static constexpr double kSmoothMs      = 50.0;   // Single vsync — acceptable
    static constexpr double kHitchyMs      = 150.0;  // Visible jolt
    static constexpr double kDisruptiveMs  = 150.0;  // Beyond this = disruption

    // Stale-frame tolerance: how long can an old frame be visible
    // before the user perceives "frozen" content.
    static constexpr double kStaleTolerance   = 100.0;  // ms
    static constexpr double kStaleWarningMs   = 200.0;  // definitely noticed
    static constexpr double kStaleCriticalMs  = 500.0;  // user assumes broken

    // Cadence convergence: how many frames at stable interval
    // before we declare pacing "settled."
    static constexpr int kStableFrameWindow   = 3;      // consecutive frames
    static constexpr double kStableVarianceMs = 8.0;    // max jitter for "stable"

    // Animation continuity: max time gap between animation frames
    // before it's classified as a "jump."
    static constexpr double kAnimJumpMs       = 33.0;   // >2 vsyncs = jump
    static constexpr double kAnimBurstMs      = 5.0;    // <5ms between frames = burst

    // Priority 4: Visual silence — renderer exists but no meaningful update.
    static constexpr double kVisualSilenceMs  = 50.0;   // 3 vsyncs without update
};

inline RecoveryClass classifyRecovery(double firstFrameMs) {
    if (firstFrameMs < PerceptualThresholds::kInstantMs)  return RecoveryClass::Instant;
    if (firstFrameMs < PerceptualThresholds::kSmoothMs)   return RecoveryClass::Smooth;
    if (firstFrameMs < PerceptualThresholds::kHitchyMs)   return RecoveryClass::Hitchy;
    return RecoveryClass::Disruptive;
}

// ============================================================
//  Priority 3: Progressive Continuity Degradation
// ============================================================

// Continuity is NOT binary. Humans perceive degradation PROGRESSIVELY.
// Each condition contributes a weighted penalty to the continuity score.
//
// Score range: 0.0 (catastrophic) to 1.0 (perfect).
// Penalties are subtractive and clamped, not multiplicative.
struct ContinuityPenalties {
    // Per-occurrence penalties (applied per event during recovery)
    static constexpr double kStaleFrame2ms     = 0.01;  // negligible
    static constexpr double kStaleFrame30ms    = 0.05;  // moderate
    static constexpr double kStaleFrame100ms   = 0.15;  // significant
    static constexpr double kPacingJitter      = 0.02;  // per high-variance frame
    static constexpr double kAnimationJump     = 0.10;  // per animation discontinuity
    static constexpr double kBlackFrame        = 0.30;  // catastrophic per occurrence
    static constexpr double kBurstFrame        = 0.005; // per burst frame (minor)
    static constexpr double kVisualSilence     = 0.03;  // per silence detection

    // Compute stale penalty progressively based on duration
    static double stalePenalty(double staleMs) {
        if (staleMs < 2.0)   return 0.0;
        if (staleMs < 30.0)  return kStaleFrame2ms;
        if (staleMs < 100.0) return kStaleFrame30ms;
        return kStaleFrame100ms;
    }
};

// ============================================================
//  Priority 2: Distribution Telemetry
// ============================================================

// Rolling distribution tracker for recovery durations.
// Computes P50, P95, variance, jitter across cycles.
struct RecoveryDistribution {
    static constexpr int kMaxSamples = 64;  // last 64 recovery cycles

    std::vector<double> coldSamples;     // cold recovery durations
    std::vector<double> totalSamples;    // total recovery durations

    void record(double coldMs, double totalMs) {
        coldSamples.push_back(coldMs);
        totalSamples.push_back(totalMs);
        if (coldSamples.size() > kMaxSamples) coldSamples.erase(coldSamples.begin());
        if (totalSamples.size() > kMaxSamples) totalSamples.erase(totalSamples.begin());
    }

    // Percentile computation (sorted copy)
    static double percentile(const std::vector<double>& data, double pct) {
        if (data.empty()) return 0.0;
        std::vector<double> sorted = data;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * pct);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }

    static double mean(const std::vector<double>& data) {
        if (data.empty()) return 0.0;
        return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    }

    static double variance(const std::vector<double>& data) {
        if (data.size() < 2) return 0.0;
        double avg = mean(data);
        double sumSq = 0.0;
        for (double v : data) { double d = v - avg; sumSq += d * d; }
        return sumSq / data.size();
    }

    static double jitter(const std::vector<double>& data) {
        if (data.size() < 2) return 0.0;
        double sumDiff = 0.0;
        for (size_t i = 1; i < data.size(); i++) {
            sumDiff += std::abs(data[i] - data[i - 1]);
        }
        return sumDiff / (data.size() - 1);
    }

    // Cold recovery stats
    double coldP50() const { return percentile(coldSamples, 0.50); }
    double coldP95() const { return percentile(coldSamples, 0.95); }
    double coldVariance() const { return variance(coldSamples); }
    double coldJitter() const { return jitter(coldSamples); }

    // Total recovery stats
    double totalP50() const { return percentile(totalSamples, 0.50); }
    double totalP95() const { return percentile(totalSamples, 0.95); }
    double totalVariance() const { return variance(totalSamples); }
    double totalJitter() const { return jitter(totalSamples); }

    int sampleCount() const { return static_cast<int>(totalSamples.size()); }
};

// ============================================================
//  Per-Renderer Recovery Tracker
// ============================================================

// Tracks the experiential recovery lifecycle for a single renderer.
// State machine: Stable → RecoveringCold → RecoveringWarm → Stable
//
// Fed by:
//   - resume notifications (from governance)
//   - frame production events (from FrameCadenceMonitor)
//   - cadence stability checks (from pacing variance)
//
// Priority 5 (Architecture prep):
//   Currently fed via broadcast from FrameCadenceMonitor.
//   Future: engine-specific attribution via engineId → rendererId mapping.
//   The onFrame() method already gates on phase, so broadcast is safe
//   but imprecise. Engine attribution will eliminate cross-engine
//   interference in burst/gap detection.
struct RecoveryTracker {
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    RecoveryPhase phase = RecoveryPhase::Stable;
    RecoveryClass lastClassification = RecoveryClass::Unknown;

    // ---- Timestamps ----
    TimePoint resumeRequestedAt;        // When governance requested resume
    TimePoint firstFrameAfterResume;    // First frame rendered post-resume
    TimePoint visuallyStableAt;         // When cadence normalized
    TimePoint lastFrameAt;              // Most recent frame
    TimePoint lastMeaningfulFrameAt;    // Most recent frame with visual change

    // ---- Durations (computed) ----
    double coldRecoveryMs = 0.0;        // resume → first frame
    double warmRecoveryMs = 0.0;        // first frame → visually stable
    double totalRecoveryMs = 0.0;       // resume → visually stable
    double staleExposureMs = 0.0;       // time old frame was visible (resume → first frame)

    // ---- Cadence convergence ----
    int stableFrameCount = 0;           // consecutive frames within pacing tolerance
    double lastFrameIntervalMs = 0.0;   // interval between last two frames
    double avgFrameIntervalMs = 0.0;    // EMA of frame intervals
    double pacingVarianceMs = 0.0;      // current pacing jitter (stddev approx)

    // ---- Animation continuity (Priority 3: progressive degradation) ----
    int continuityBreaks = 0;           // frames with timeline jump
    int burstFrames = 0;                // frames produced too fast (catch-up)
    double maxFrameGapMs = 0.0;         // largest inter-frame gap during recovery
    double continuityScore = 1.0;       // 0.0=catastrophic, 1.0=perfect (graded)

    // ---- Priority 1: Split panic semantics ----
    // recoveryBurstObserved: DESCRIPTIVE — many frame notifications clustered tightly.
    //   Pure telemetry fact. Does NOT imply perceptual failure.
    bool recoveryBurstObserved = false;
    int burstEpisodes = 0;              // how many burst clusters detected

    // experientialInstability: EVALUATIVE — actual perceptual concern detected.
    //   Based on: pacing collapse, visible discontinuity, stale persistence,
    //   animation jump, cadence instability during recovery.
    bool experientialInstability = false;
    const char* instabilityReason = "none";

    // ---- Priority 4: Visual silence ----
    // Time where renderer exists but no visually meaningful update occurred.
    // Distinct from stale: stale = old frame visible after resume.
    // Silence = renderer technically producing but no perceptual change.
    double visualSilenceMs = 0.0;       // accumulated silence during recovery
    int visualSilenceEvents = 0;        // count of silence detections

    // ---- Priority 2: Distribution telemetry ----
    RecoveryDistribution distribution;

    // ---- Lifecycle counters ----
    int totalRecoveryCycles = 0;        // how many times this renderer recovered
    double bestRecoveryMs = -1.0;       // best-ever total recovery time
    double worstRecoveryMs = -1.0;      // worst-ever total recovery time

    // ── State machine: BEGIN RECOVERY ──
    // Called when governance transitions a renderer to Active from Parked/Dormant.
    void beginRecovery() {
        phase = RecoveryPhase::RecoveringCold;
        resumeRequestedAt = Clock::now();
        firstFrameAfterResume = {};
        visuallyStableAt = {};
        coldRecoveryMs = 0.0;
        warmRecoveryMs = 0.0;
        totalRecoveryMs = 0.0;
        staleExposureMs = 0.0;
        stableFrameCount = 0;
        pacingVarianceMs = 999.0; // unknown
        continuityBreaks = 0;
        burstFrames = 0;
        maxFrameGapMs = 0.0;
        continuityScore = 1.0;
        lastClassification = RecoveryClass::Unknown;

        // Priority 1: Reset per-recovery burst/instability
        recoveryBurstObserved = false;
        burstEpisodes = 0;
        experientialInstability = false;
        instabilityReason = "none";

        // Priority 4: Reset visual silence
        visualSilenceMs = 0.0;
        visualSilenceEvents = 0;
    }

    // ── State machine: FRAME RECEIVED ──
    // Called when a new frame is produced by this renderer.
    // IMPORTANT: Only processes during recovery phases.
    // During Stable operation, frames are normal — not recovery evidence.
    void onFrame() {
        // Skip processing in non-recovery phases.
        // The broadcast model sends frame events from ALL engines,
        // so without this gate, stable renderers accumulate false
        // burst/gap detections from cross-engine frame timing.
        if (phase == RecoveryPhase::Stable ||
            phase == RecoveryPhase::VisuallyStable) return;

        auto now = Clock::now();

        if (phase == RecoveryPhase::RecoveringCold) {
            // First frame after resume!
            firstFrameAfterResume = now;
            coldRecoveryMs = static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - resumeRequestedAt).count()) / 1000.0;
            staleExposureMs = coldRecoveryMs; // old frame was visible this long

            // Priority 3: Apply stale penalty progressively
            continuityScore -= ContinuityPenalties::stalePenalty(staleExposureMs);

            // Classify the cold recovery
            lastClassification = classifyRecovery(coldRecoveryMs);

            phase = RecoveryPhase::RecoveringWarm;
            stableFrameCount = 0;
            avgFrameIntervalMs = 0.0;
            lastFrameAt = now;
            lastMeaningfulFrameAt = now;
            return;
        }

        if (phase == RecoveryPhase::RecoveringWarm) {

            // Compute frame interval
            double intervalMs = 0.0;
            if (lastFrameAt.time_since_epoch().count() > 0) {
                intervalMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - lastFrameAt).count()) / 1000.0;
            }
            lastFrameIntervalMs = intervalMs;

            // ---- Priority 3: Progressive continuity degradation ----

            // Animation jump detection
            if (intervalMs > PerceptualThresholds::kAnimJumpMs && intervalMs > 0.0) {
                continuityBreaks++;
                continuityScore -= ContinuityPenalties::kAnimationJump;
            }

            // Burst detection (DESCRIPTIVE ONLY — does NOT affect continuity score)
            // Burst frames are cadence-model observations, not perceptual degradation.
            // The broadcast model naturally causes cross-engine burst clustering.
            if (intervalMs < PerceptualThresholds::kAnimBurstMs && intervalMs > 0.0) {
                burstFrames++;
                // NO continuityScore penalty — burst is not experiential degradation
                if (burstFrames >= 5) {
                    recoveryBurstObserved = true;
                    burstEpisodes++;
                }
            } else {
                burstFrames = 0; // reset burst counter on normal frame
            }

            if (intervalMs > maxFrameGapMs) maxFrameGapMs = intervalMs;

            // Priority 4: Visual silence detection
            if (intervalMs > PerceptualThresholds::kVisualSilenceMs) {
                visualSilenceMs += intervalMs;
                visualSilenceEvents++;
                continuityScore -= ContinuityPenalties::kVisualSilence;
            }

            // EMA of interval for pacing stability
            if (avgFrameIntervalMs == 0.0) {
                avgFrameIntervalMs = intervalMs;
            } else {
                avgFrameIntervalMs = avgFrameIntervalMs * 0.7 + intervalMs * 0.3;
            }

            // Pacing variance (simple deviation from EMA)
            double deviation = std::abs(intervalMs - avgFrameIntervalMs);
            pacingVarianceMs = pacingVarianceMs * 0.7 + deviation * 0.3;

            // Priority 3: Pacing jitter penalty
            if (deviation > PerceptualThresholds::kStableVarianceMs) {
                continuityScore -= ContinuityPenalties::kPacingJitter;
            }

            // Check cadence convergence
            if (pacingVarianceMs < PerceptualThresholds::kStableVarianceMs) {
                stableFrameCount++;
            } else {
                stableFrameCount = 0;
            }

            // Transition RecoveringWarm → Stable
            // Once cadence converges, recovery is complete.
            if (stableFrameCount >= PerceptualThresholds::kStableFrameWindow) {

                visuallyStableAt = now;
                warmRecoveryMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - firstFrameAfterResume).count()) / 1000.0;
                totalRecoveryMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - resumeRequestedAt).count()) / 1000.0;

                // Reclassify with full recovery time
                lastClassification = classifyRecovery(totalRecoveryMs);

                // Clamp continuity score
                continuityScore = (std::max)(0.0, (std::min)(1.0, continuityScore));

                // Priority 1: Evaluate experiential instability
                // This is PERCEPTUAL JUDGMENT, not cadence observation.
                evaluateInstability();

                // Update best/worst
                totalRecoveryCycles++;
                if (bestRecoveryMs < 0.0 || totalRecoveryMs < bestRecoveryMs) {
                    bestRecoveryMs = totalRecoveryMs;
                }
                if (worstRecoveryMs < 0.0 || totalRecoveryMs > worstRecoveryMs) {
                    worstRecoveryMs = totalRecoveryMs;
                }

                // Priority 2: Record distribution sample
                distribution.record(coldRecoveryMs, totalRecoveryMs);

                // Go directly to Stable — recovery is complete.
                phase = RecoveryPhase::Stable;
            }
        }

        lastFrameAt = now;
    }

    // ── Priority 1: Experiential Instability Evaluation ──
    // Called at recovery completion. Produces a PERCEPTUAL JUDGMENT.
    // Separate from burst observation (which is descriptive telemetry).
    void evaluateInstability() {
        experientialInstability = false;
        instabilityReason = "none";

        // Check perceptual failure conditions in severity order
        if (continuityScore < 0.5) {
            experientialInstability = true;
            instabilityReason = "continuity_collapsed";
        } else if (continuityBreaks >= 3) {
            experientialInstability = true;
            instabilityReason = "repeated_discontinuity";
        } else if (staleExposureMs > PerceptualThresholds::kStaleWarningMs) {
            experientialInstability = true;
            instabilityReason = "stale_exposure";
        } else if (visualSilenceMs > PerceptualThresholds::kStaleTolerance) {
            experientialInstability = true;
            instabilityReason = "visual_silence";
        } else if (maxFrameGapMs > PerceptualThresholds::kHitchyMs) {
            experientialInstability = true;
            instabilityReason = "frame_gap";
        } else if (lastClassification == RecoveryClass::Disruptive) {
            experientialInstability = true;
            instabilityReason = "disruptive_recovery";
        }
        // NOTE: recoveryBurstObserved does NOT trigger instability.
        // Burst is a cadence-model artifact, not a perceptual failure.
    }

    // ── State machine: STALE CHECK ──
    // Called periodically to detect stale-frame conditions.
    // Returns true if the renderer is exposing a stale frame.
    bool checkStale() {
        if (phase == RecoveryPhase::Stable || phase == RecoveryPhase::VisuallyStable) {
            if (lastFrameAt.time_since_epoch().count() > 0) {
                auto now = Clock::now();
                double sinceLast = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastFrameAt).count());
                if (sinceLast > PerceptualThresholds::kStaleTolerance) {
                    phase = RecoveryPhase::StaleVisible;
                    staleExposureMs = sinceLast;
                    return true;
                }
            }
        }
        return false;
    }

    // ── Continuity score clamped ──
    double clampedContinuityScore() const {
        return (std::max)(0.0, (std::min)(1.0, continuityScore));
    }
};

// ============================================================
//  Pre-Fix 2: Recovery Saturation Telemetry
// ============================================================

// Tracks how close the system is to recovery failure.
// NOT recovery quality — recovery CAPACITY.
//
// This becomes the FIRST real adaptive-pressure signal for Phase 3D.
// Concurrency invariance was proven at 6 engines,
// but saturation tells us how much headroom remains.
//
// Computed by WorkloadController by scanning all RecoveryTrackers.
struct RecoverySaturation {
    int concurrentRecoveries = 0;    // renderers currently in RecoveringCold/RecoveringWarm
    int pendingWakes = 0;            // wakes requested but deferred (Pre-Fix 3)
    int peakConcurrency = 0;         // highest concurrent recovery count ever
    int totalEngines = 0;            // total registered engines

    // Saturation ratio: 0.0 = no recoveries, 1.0 = all engines recovering
    // This is temporal pressure, not topological count.
    double saturationRatio() const {
        if (totalEngines <= 0) return 0.0;
        return static_cast<double>(concurrentRecoveries) / totalEngines;
    }

    // Is the system under recovery pressure?
    bool isElevated() const { return saturationRatio() > 0.3; }
    bool isConstrained() const { return saturationRatio() > 0.6; }
    bool isCritical() const { return saturationRatio() > 0.8; }
};

// ============================================================
//  Recovery Experience Report (Aggregate)
// ============================================================

// Aggregate experiential report across all renderers.
// Produced by scanning all RecoveryTrackers.
struct RecoveryExperienceReport {
    int totalRenderers = 0;
    int recoveringCount = 0;       // currently in recovery
    int staleCount = 0;            // currently showing stale frames
    int continuityBrokenCount = 0; // have continuity breaks

    // Worst-case metrics
    double worstColdRecoveryMs = 0.0;
    double worstWarmRecoveryMs = 0.0;
    double worstTotalRecoveryMs = 0.0;
    double worstStaleExposureMs = 0.0;
    double worstPacingVarianceMs = 0.0;
    double worstMaxFrameGapMs = 0.0;
    double lowestContinuityScore = 1.0;
    int totalContinuityBreaks = 0;
    int totalBurstFrames = 0;

    // Priority 1: Split panic semantics
    bool anyBurstObserved = false;         // descriptive — burst cadence detected
    int totalBurstEpisodes = 0;
    bool anyExperientialInstability = false; // evaluative — perceptual failure
    const char* worstInstabilityReason = "none";

    // Priority 4: Visual silence
    double totalVisualSilenceMs = 0.0;
    int totalVisualSilenceEvents = 0;

    // Classification distribution
    int instantCount = 0;
    int smoothCount = 0;
    int hitchyCount = 0;
    int disruptiveCount = 0;
    int unknownCount = 0;

    // Best/worst across all renderers, all cycles
    double bestEverRecoveryMs = -1.0;
    double worstEverRecoveryMs = -1.0;

    // Priority 2: Distribution telemetry (aggregate across renderers)
    double coldP50 = 0.0;
    double coldP95 = 0.0;
    double coldVariance = 0.0;
    double coldJitter = 0.0;
    double totalP50 = 0.0;
    double totalP95 = 0.0;
    double totalVariance = 0.0;
    double totalJitter = 0.0;
    int distributionSamples = 0;

    // Pre-Fix 2: Recovery saturation (snapshot)
    RecoverySaturation saturation;

    // Pre-Fix 3: Recovery ordering telemetry
    int deferredWakeCount = 0;     // wakes delayed by concurrency limits
    int protectedWakeCount = 0;    // MustRemainWarm survived budget pressure

    // Dominant experiential issue
    const char* dominantIssue = "none";

    // Determine dominant issue from metrics
    void classifyDominantIssue() {
        // Priority 1: Perceptual instability is the PRIMARY signal.
        // Burst observation is secondary and does NOT dominate.
        if (anyExperientialInstability) {
            dominantIssue = worstInstabilityReason;
        } else if (worstStaleExposureMs > PerceptualThresholds::kStaleCriticalMs) {
            dominantIssue = "stale_critical";
        } else if (totalContinuityBreaks > 3) {
            dominantIssue = "continuity_degraded";
        } else if (disruptiveCount > 0) {
            dominantIssue = "disruptive_wake";
        } else if (worstStaleExposureMs > PerceptualThresholds::kStaleWarningMs) {
            dominantIssue = "stale_warning";
        } else if (hitchyCount > 0) {
            dominantIssue = "hitchy_wake";
        } else if (worstPacingVarianceMs > PerceptualThresholds::kStableVarianceMs * 2) {
            dominantIssue = "pacing_unstable";
        } else if (totalVisualSilenceEvents > 0) {
            dominantIssue = "visual_silence";
        } else if (anyBurstObserved) {
            // Burst is LAST — descriptive, not evaluative
            dominantIssue = "burst_observed";
        } else {
            dominantIssue = "none";
        }
    }
};

}  // namespace morphic

