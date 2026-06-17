#pragma once

#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include "types.h"
#include "runtime_mutation_intent.h"

namespace morphic {

struct TraceFrame {
    uint64_t epoch = 0;
    uint64_t timestampMs = 0;
    std::vector<RuntimeMutationIntent> intentsProcessed;
    
    // External observations
    bool mockHwndCreateSuccess = true;
    bool mockActivationSuccess = true;
    uint64_t injectedDelayMs = 0;
    
    // Health status
    double temporalHealth = 1.0;
    double semanticHealth = 1.0;
    double operationalHealth = 1.0;
    double realizationHealth = 1.0;
    int confidence = 0; // 0 = High, 1 = Degraded, 2 = Uncertain
};

struct TraceInjectionConfig {
    bool forceHwndCreationFailure = false;
    bool forceActivationDenial = false;
    uint64_t timingJitterSkewMs = 0;
};

class KernelTrace {
public:
    KernelTrace(size_t ringBufferSize = 2000);
    ~KernelTrace() = default;

    // Recording API
    void recordFrame(const TraceFrame& frame);
    void setOptInPersistence(bool optIn, const std::string& filepath = "");
    
    // Controls
    void freezeTraceCapture();
    void flushSnapshot(const std::string& filepath);
    bool loadSnapshot(const std::string& filepath);

    // Replay API (Constrained Replay Scope: Passive Inject Only)
    void enableReplayMode(bool enable, const TraceInjectionConfig& config = {});
    bool isReplayMode() const { return replayMode_; }
    const TraceInjectionConfig& injectionConfig() const { return injectionConfig_; }

    const std::vector<TraceFrame>& getRingBuffer() const { return ringBuffer_; }
    
private:
    mutable std::mutex mutex_;
    size_t ringBufferSize_;
    std::vector<TraceFrame> ringBuffer_;
    size_t head_ = 0;
    
    bool frozen_ = false;
    bool optInPersistence_ = false;
    std::string persistentPath_;
    
    bool replayMode_ = false;
    TraceInjectionConfig injectionConfig_;
};

} // namespace morphic
