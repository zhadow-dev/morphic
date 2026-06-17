#include "kernel_trace.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace morphic {

KernelTrace::KernelTrace(size_t ringBufferSize)
    : ringBufferSize_(ringBufferSize) {
    ringBuffer_.reserve(ringBufferSize_);
}

void KernelTrace::recordFrame(const TraceFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frozen_) return;

    if (ringBuffer_.size() < ringBufferSize_) {
        ringBuffer_.push_back(frame);
    } else {
        ringBuffer_[head_] = frame;
        head_ = (head_ + 1) % ringBufferSize_;
    }

    if (optInPersistence_ && !persistentPath_.empty()) {
        std::ofstream out(persistentPath_, std::ios::app);
        if (out.is_open()) {
            out << "F " << frame.epoch << " "
                << frame.timestampMs << " "
                << frame.mockHwndCreateSuccess << " "
                << frame.mockActivationSuccess << " "
                << frame.injectedDelayMs << " "
                << frame.temporalHealth << " "
                << frame.semanticHealth << " "
                << frame.operationalHealth << " "
                << frame.realizationHealth << " "
                << frame.confidence << " "
                << frame.intentsProcessed.size() << "\n";
            for (const auto& intent : frame.intentsProcessed) {
                out << "I " << static_cast<int>(intent.type) << " "
                    << intent.surfaceId << " "
                    << intent.workspaceId.value << " "
                    << static_cast<int>(intent.priority) << " "
                    << intent.enqueueEpoch << " "
                    << intent.ageTicks << " "
                    << intent.surfaceEpoch << " "
                    << intent.geometry.x << " "
                    << intent.geometry.y << " "
                    << intent.geometry.width << " "
                    << intent.geometry.height << " "
                    << intent.visible << " "
                    << static_cast<int>(intent.role) << " "
                    << static_cast<int>(intent.elevation) << " "
                    << intent.sublevel << " "
                    << intent.active << " "
                    << static_cast<int>(intent.continuity) << " "
                    << static_cast<int>(intent.attention) << " "
                    << static_cast<int>(intent.semanticVisibility) << " "
                    << static_cast<int>(intent.presence) << "\n";
            }
        }
    }
}

void KernelTrace::setOptInPersistence(bool optIn, const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    optInPersistence_ = optIn;
    persistentPath_ = filepath;
    if (optIn && !filepath.empty()) {
        std::ofstream out(filepath, std::ios::trunc);
    }
}

void KernelTrace::freezeTraceCapture() {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_ = true;
}

void KernelTrace::flushSnapshot(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(filepath, std::ios::trunc);
    if (!out.is_open()) return;

    size_t count = ringBuffer_.size();
    size_t start = (count < ringBufferSize_) ? 0 : head_;
    
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (start + i) % ringBufferSize_;
        const auto& frame = ringBuffer_[idx];
        out << "F " << frame.epoch << " "
            << frame.timestampMs << " "
            << frame.mockHwndCreateSuccess << " "
            << frame.mockActivationSuccess << " "
            << frame.injectedDelayMs << " "
            << frame.temporalHealth << " "
            << frame.semanticHealth << " "
            << frame.operationalHealth << " "
            << frame.realizationHealth << " "
            << frame.confidence << " "
            << frame.intentsProcessed.size() << "\n";
        for (const auto& intent : frame.intentsProcessed) {
            out << "I " << static_cast<int>(intent.type) << " "
                << intent.surfaceId << " "
                << intent.workspaceId.value << " "
                << static_cast<int>(intent.priority) << " "
                << intent.enqueueEpoch << " "
                << intent.ageTicks << " "
                << intent.surfaceEpoch << " "
                << intent.geometry.x << " "
                << intent.geometry.y << " "
                << intent.geometry.width << " "
                << intent.geometry.height << " "
                << intent.visible << " "
                << static_cast<int>(intent.role) << " "
                << static_cast<int>(intent.elevation) << " "
                << intent.sublevel << " "
                << intent.active << " "
                << static_cast<int>(intent.continuity) << " "
                << static_cast<int>(intent.attention) << " "
                << static_cast<int>(intent.semanticVisibility) << " "
                << static_cast<int>(intent.presence) << "\n";
        }
    }
}

bool KernelTrace::loadSnapshot(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream in(filepath);
    if (!in.is_open()) return false;

    ringBuffer_.clear();
    head_ = 0;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        char typeChar;
        if (!(ss >> typeChar)) continue;
        if (typeChar == 'F') {
            TraceFrame frame;
            size_t intentsCount = 0;
            int confidenceInt = 0;
            if (!(ss >> frame.epoch
                     >> frame.timestampMs
                     >> frame.mockHwndCreateSuccess
                     >> frame.mockActivationSuccess
                     >> frame.injectedDelayMs
                     >> frame.temporalHealth
                     >> frame.semanticHealth
                     >> frame.operationalHealth
                     >> frame.realizationHealth
                     >> confidenceInt
                     >> intentsCount)) {
                continue;
            }
            frame.confidence = confidenceInt;

            bool frameOk = true;
            for (size_t i = 0; i < intentsCount; ++i) {
                if (!std::getline(in, line)) {
                    frameOk = false;
                    break;
                }
                std::stringstream ssIntent(line);
                char intChar;
                if (!(ssIntent >> intChar) || intChar != 'I') {
                    frameOk = false;
                    break;
                }

                RuntimeMutationIntent intent;
                int typeInt, priorityInt, roleInt, elevationInt, continuityInt, attentionInt, semVisInt, presenceInt;
                if (!(ssIntent >> typeInt
                              >> intent.surfaceId
                              >> intent.workspaceId.value
                              >> priorityInt
                              >> intent.enqueueEpoch
                              >> intent.ageTicks
                              >> intent.surfaceEpoch
                              >> intent.geometry.x
                              >> intent.geometry.y
                              >> intent.geometry.width
                              >> intent.geometry.height
                              >> intent.visible
                              >> roleInt
                              >> elevationInt
                              >> intent.sublevel
                              >> intent.active
                              >> continuityInt
                              >> attentionInt
                              >> semVisInt
                              >> presenceInt)) {
                    frameOk = false;
                    break;
                }

                intent.type = static_cast<RuntimeMutationIntent::Type>(typeInt);
                intent.priority = static_cast<MutationPriority>(priorityInt);
                intent.role = static_cast<SurfaceRole>(roleInt);
                intent.elevation = static_cast<ElevationLayer>(elevationInt);
                intent.continuity = static_cast<ContinuityState>(continuityInt);
                intent.attention = static_cast<AttentionLevel>(attentionInt);
                intent.semanticVisibility = static_cast<SemanticVisibility>(semVisInt);
                intent.presence = static_cast<RuntimePresence>(presenceInt);

                frame.intentsProcessed.push_back(intent);
            }
            if (frameOk) {
                ringBuffer_.push_back(frame);
            }
        }
    }
    return true;
}

void KernelTrace::enableReplayMode(bool enable, const TraceInjectionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    replayMode_ = enable;
    injectionConfig_ = config;
}

} // namespace morphic
